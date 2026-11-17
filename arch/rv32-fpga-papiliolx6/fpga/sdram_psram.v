// sdram_psram: a controller that makes an SDR SDRAM (16 bit) look like a PSRAM.
//   From the CPU side (XBUS, Wishbone-like) this is "a big RAM where you put out an address and wait for ack".
//   Initialisation, refresh, opening and closing rows, bursts and auto precharge are all hidden here.
//   The XBUS stb lasts only one cycle (cyc is held until ack), so a request is latched at any time and kept in pend.
//   One access = ACTIVE -> READ/WRITE (burst length 2 = one 32-bit word, auto precharge) -> wait.
//   No optimisation that keeps a row open (slower, but there is no need to think about conflicts with refresh).
//
//   Address (bytes): [1:0] select bytes (seen through sel), w = adr[24:2] (the 32-bit word number)
//     col = {w[COL_BITS-2:0], beat}, bank = the 2 bits above it, row = the ROW_BITS bits above that
//     The default is the Micron MT48LC4M16 of the Papilio Pro (64Mbit = 12 rows x 8 columns x 4 banks x 16 bits = 8MB).
//   For a different SDRAM change ROW_BITS/COL_BITS (A10 is used for auto precharge, so COL_BITS <= 10).
//
//   The initialisation wait (200us) and the refresh interval (7.5us) are derived from CLK_HZ.
//   The CAS latency is fixed at 2; the timings are cycle counts on the safe side (parameters).
module sdram_psram #(
  parameter CLK_HZ  = 32000000,
  parameter ROW_BITS = 12,
  parameter COL_BITS = 8,
  parameter T_RCD   = 2,      // ACTIVE → READ/WRITE
  parameter T_RP    = 2,      // PRECHARGE → ACTIVE
  parameter T_RFC   = 3,      // REFRESH -> next command
  parameter T_WR    = 2       // last write data -> PRECHARGE
) (
  input  wire [2:0]  cap,        // the beat, counted from READ, at which the first data beat is captured (3 in simulation, 2 or so on the board)
  input  wire        clk,
  input  wire        rst,        // synchronous, reset when High
  // CPU side (XBUS)
  input  wire        cyc,
  input  wire        stb,
  input  wire        we,
  input  wire [3:0]  sel,
  input  wire [24:0] adr,        // byte address (start of RAM = 0)
  input  wire [31:0] dati,       // CPU → RAM
  output reg  [31:0] dato,       // RAM → CPU
  output reg         ack,
  // SDRAM side
  output wire        sd_cke,
  output reg         sd_cs_n,
  output reg         sd_ras_n,
  output reg         sd_cas_n,
  output reg         sd_we_n,
  output reg  [1:0]  sd_ba,
  output reg  [12:0] sd_a,
  output reg  [1:0]  sd_dqm,
  output reg  [15:0] sd_dq_o,
  output reg         sd_dq_oe,
  input  wire [15:0] sd_dq_i,
  output wire        ready       // initialisation is done
);
  assign sd_cke = 1'b1;
  initial begin sd_cs_n = 1; sd_ras_n = 1; sd_cas_n = 1; sd_we_n = 1; sd_dqm = 2'b11; sd_dq_oe = 0; sd_dq_o = 0; sd_ba = 0; sd_a = 0; ack = 0; dato = 0; end

  localparam [3:0] CMD_NOP = 4'b0111, CMD_ACT = 4'b0011, CMD_RD = 4'b0101,
                   CMD_WR  = 4'b0100, CMD_PRE = 4'b0010, CMD_REF = 4'b0001, CMD_MRS = 4'b0000;

  localparam INIT_WAIT = CLK_HZ / 5000;        // 200us
  localparam REFI      = CLK_HZ / 166666;      // 6us (fits the required 7.8125us even with the wait for one access added)

  localparam S_INIT0 = 0, S_PRE = 1, S_REF1 = 2, S_REF2 = 3, S_MRS = 4, S_IDLE = 5,
             S_RCD = 6, S_RDW = 7, S_WRW = 8, S_RFC = 9, S_REC = 10, S_DONE = 11;
  reg [3:0]  st;
  reg [15:0] cnt;
  reg [15:0] rcnt;
  reg        rdue;
  reg [24:0] la; reg lwe; reg [3:0] lsel; reg [31:0] ld;   // the accepted request (stb lasts one cycle only, so latch it at any time)
  reg        pend;
  reg        init_done;
  assign ready = init_done;

  localparam CB = COL_BITS - 1;                 // column bits of a word (without the beat)
  wire [22:0] w   = la[24:2];
  wire [COL_BITS-1:0] col = {w[CB-1:0], 1'b0};
  wire [1:0]  bk  = w[CB+1:CB];
  wire [ROW_BITS-1:0] row = w[CB+1+ROW_BITS:CB+2];

  task cmd(input [3:0] c);
    begin {sd_cs_n, sd_ras_n, sd_cas_n, sd_we_n} <= c; end
  endtask

  always @(posedge clk) begin
    if (rst) begin
      st <= S_INIT0; cnt <= INIT_WAIT; rcnt <= 0; rdue <= 0; ack <= 0; init_done <= 0; pend <= 0;
      cmd(CMD_NOP); sd_ba <= 0; sd_a <= 0; sd_dqm <= 2'b11; sd_dq_oe <= 0; sd_dq_o <= 0; dato <= 0;
    end else begin
      cmd(CMD_NOP); ack <= 0; sd_dq_oe <= 0; sd_dqm <= 2'b00;
      if (cyc && stb) begin la <= adr; lwe <= we; lsel <= sel; ld <= dati; pend <= 1; end
      // refresh interval
      if (init_done) begin
        if (rcnt >= REFI - 1) begin rcnt <= 0; rdue <= 1; end else rcnt <= rcnt + 1;
      end
      case (st)
        S_INIT0: begin
          sd_dqm <= 2'b11;
          if (cnt == 0) begin cmd(CMD_PRE); sd_a <= 13'h0400; st <= S_PRE; cnt <= T_RP - 1; end
          else cnt <= cnt - 1;
        end
        S_PRE: begin
          sd_dqm <= 2'b11;
          if (cnt == 0) begin cmd(CMD_REF); st <= S_REF1; cnt <= T_RFC - 1; end
          else cnt <= cnt - 1;
        end
        S_REF1: begin
          sd_dqm <= 2'b11;
          if (cnt == 0) begin cmd(CMD_REF); st <= S_REF2; cnt <= T_RFC - 1; end
          else cnt <= cnt - 1;
        end
        S_REF2: begin
          sd_dqm <= 2'b11;
          if (cnt == 0) begin
            cmd(CMD_MRS); sd_ba <= 2'b00; sd_a <= 13'h0021;    // BL=2, sequential, CL=2
            st <= S_MRS; cnt <= 2;
          end else cnt <= cnt - 1;
        end
        S_MRS: begin
          sd_dqm <= 2'b11;
          if (cnt == 0) begin st <= S_IDLE; init_done <= 1; rcnt <= 0; end
          else cnt <= cnt - 1;
        end
        S_IDLE: begin
          if (rdue) begin
            cmd(CMD_REF); rdue <= 0; st <= S_RFC; cnt <= T_RFC - 1;
          end else if (pend) begin
            pend <= 0;
            cmd(CMD_ACT); sd_ba <= bk; sd_a <= {{(13-ROW_BITS){1'b0}}, row};
            st <= S_RCD; cnt <= T_RCD - 1;
          end
        end
        S_RFC: begin
          if (cnt == 0) st <= S_IDLE; else cnt <= cnt - 1;
        end
        S_RCD: begin
          if (cnt == 0) begin
            sd_ba <= bk; sd_a <= (13'h0400 | col);               // A10=1: auto precharge
            if (lwe) begin
              cmd(CMD_WR);
              sd_dq_oe <= 1; sd_dq_o <= ld[15:0]; sd_dqm <= ~lsel[1:0];
              st <= S_WRW; cnt <= 0;
            end else begin
              cmd(CMD_RD);
              st <= S_RDW; cnt <= 0;
            end
          end else cnt <= cnt - 1;
        end
        S_WRW: begin
          // second beat of data; then wait tWR + tRP and ack
          sd_dq_oe <= 1; sd_dq_o <= ld[31:16]; sd_dqm <= ~lsel[3:2];
          st <= S_REC; cnt <= T_WR + T_RP;
        end
        S_RDW: begin
          // count from the beat after READ is issued. cnt 1 -> ...: the first data beat is 4 beats after READ (cnt==3), the second is 5 beats after (cnt==4)
          cnt <= cnt + 1;
          if (cnt == {13'b0, cap}) dato[15:0]  <= sd_dq_i;
          if (cnt == {13'b0, cap} + 16'd1) begin dato[31:16] <= sd_dq_i; st <= S_REC; cnt <= T_RP + 3 - {13'b0, cap}; end
        end
        S_REC: begin
          if (cnt == 0) begin ack <= 1; st <= S_IDLE; end
          else cnt <= cnt - 1;
        end
        default: st <= S_INIT0;
      endcase
    end
  end
endmodule
