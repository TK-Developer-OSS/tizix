#!/bin/sh
# docker/ec2-launch.sh - Docker イメージを作るための EC2 を一時的に立てる(#74)
#
#   **鉄の掟: 最大 6 時間で必ず止まる**。互いに独立した 3 重にしてある:
#     1) インスタンス内: 初回起動時に期限(+6h)をディスクに刻み、systemd タイマーが毎分見て
#        過ぎていれば poweroff。再起動しても期限は延びない(shutdown -h +360 と違い予約が消えない)。
#        InstanceInitiatedShutdownBehavior=stop なので OS が落ちればインスタンスは「停止」になる。
#     2) 起動元(このスクリプトを走らせたホスト): 5 時間 55 分後に stop-instances を撃つ見張りを残す。
#     3) 起動直後に SSH でタイマーが実際に登録されたか確かめ、無ければその場で止めて作業しない。
#
#   使い方: AWS_PROFILE=... sh docker/ec2-launch.sh
#   前提: 大阪(ap-northeast-3)、c7i.2xlarge、Ubuntu 24.04 公式 AMI。鍵は ~/.ssh/tizix-build-osaka.pem。
set -eu
export AWS_REGION=${AWS_REGION:-ap-northeast-3}
TYPE=c7i.2xlarge
NAME=tizix-build
KEY=tizix-build-osaka
PEM=$HOME/.ssh/$KEY.pem
SG_NAME=tizix-build-ssh
LIMIT_SEC=21600          # 6 時間(インスタンス内の期限)
WATCH_SEC=21300          # 5 時間 55 分(起動元の見張り。内側より先に効く)
STATE=$HOME/.tizix-ec2   # インスタンス ID などを残す
mkdir -p "$STATE"

# Ubuntu 24.04 公式 AMI(Canonical のアカウント 099720109477 が出しているものの最新)。
# SSM パラメータで引く方法もあるが、ssm:GetParameter の権限が要るので EC2 の Describe で済ませる。
AMI=$(aws ec2 describe-images --owners 099720109477 \
      --filters "Name=name,Values=ubuntu/images/hvm-ssd-gp3/ubuntu-noble-24.04-amd64-server-*" \
                "Name=state,Values=available" \
      --query "sort_by(Images,&CreationDate)[-1].[ImageId,Name]" --output text)
echo "AMI $AMI"
AMI=$(echo "$AMI" | cut -f1)
case "$AMI" in ami-*) ;; *) echo "AMI が見つからない"; exit 1 ;; esac

# 鍵(無ければ作る)
if [ ! -f "$PEM" ]; then
    aws ec2 create-key-pair --key-name "$KEY" --key-type ed25519 --query KeyMaterial --output text > "$PEM"
    chmod 600 "$PEM"
fi

# SSH は起動元の IP からだけ
MYIP=$(curl -s https://checkip.amazonaws.com)
VPC=$(aws ec2 describe-vpcs --filters Name=is-default,Values=true --query "Vpcs[0].VpcId" --output text)
SG=$(aws ec2 describe-security-groups --filters Name=group-name,Values=$SG_NAME Name=vpc-id,Values=$VPC \
     --query "SecurityGroups[0].GroupId" --output text)
if [ "$SG" = "None" ]; then
    SG=$(aws ec2 create-security-group --group-name $SG_NAME --description "tizix image build: ssh from builder" \
         --vpc-id "$VPC" --query GroupId --output text)
fi
aws ec2 authorize-security-group-ingress --group-id "$SG" --protocol tcp --port 22 --cidr "$MYIP/32" 2>/dev/null || true

# 1) インスタンス内の期限(user-data。初回起動で 1 回だけ走り、タイマーは再起動後も残る)
cat > "$STATE/user-data.sh" <<EOF
#!/bin/bash
set -e
D=/var/lib/tizix-deadline
[ -f \$D ] || echo \$(( \$(date +%s) + $LIMIT_SEC )) > \$D
cat > /usr/local/sbin/tizix-deadline-check <<'EOS'
#!/bin/sh
[ "\$(date +%s)" -ge "\$(cat /var/lib/tizix-deadline)" ] && exec systemctl poweroff
exit 0
EOS
chmod 755 /usr/local/sbin/tizix-deadline-check
cat > /etc/systemd/system/tizix-deadline.service <<'EOS'
[Unit]
Description=tizix build: power off after the 6h deadline
[Service]
Type=oneshot
ExecStart=/usr/local/sbin/tizix-deadline-check
EOS
cat > /etc/systemd/system/tizix-deadline.timer <<'EOS'
[Unit]
Description=tizix build: check the 6h deadline every minute
[Timer]
OnBootSec=1min
OnUnitActiveSec=1min
[Install]
WantedBy=timers.target
EOS
systemctl daemon-reload
systemctl enable --now tizix-deadline.timer
EOF

ID=$(aws ec2 run-instances --image-id "$AMI" --instance-type $TYPE --key-name "$KEY" \
     --security-group-ids "$SG" --instance-initiated-shutdown-behavior stop \
     --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=40,VolumeType=gp3,DeleteOnTermination=true}' \
     --user-data "file://$STATE/user-data.sh" \
     --tag-specifications "ResourceType=instance,Tags=[{Key=Name,Value=$NAME}]" \
     --query "Instances[0].InstanceId" --output text)
echo "$ID" > "$STATE/instance-id"
echo "instance $ID ($TYPE) launched $(date '+%F %T')"

# 2) 起動元の見張り
nohup setsid sh -c "sleep $WATCH_SEC; aws ec2 stop-instances --instance-ids $ID > $STATE/watchdog.log 2>&1" \
    > /dev/null 2>&1 < /dev/null &
echo "watchdog: stop-instances at $(date -d "+$WATCH_SEC sec" '+%F %T')"

aws ec2 wait instance-running --instance-ids "$ID"
IP=$(aws ec2 describe-instances --instance-ids "$ID" --query "Reservations[0].Instances[0].PublicIpAddress" --output text)
echo "$IP" > "$STATE/ip"
echo "ip $IP"

# 3) タイマーが本当に効いているか。確かめられなければ止める
ok=
for i in $(seq 1 30); do
    if ssh -i "$PEM" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 ubuntu@"$IP" \
           'systemctl is-active tizix-deadline.timer && echo deadline=$(date -d @$(cat /var/lib/tizix-deadline) "+%F %T %Z")' \
           2>/dev/null | tee "$STATE/deadline-check" | grep -q '^deadline='; then
        ok=1; break
    fi
    sleep 10
done
if [ -z "$ok" ]; then
    echo "期限タイマーを確認できない → 止める"
    aws ec2 stop-instances --instance-ids "$ID" > /dev/null
    exit 1
fi
cat "$STATE/deadline-check"
echo "ready: ssh -i $PEM ubuntu@$IP"
