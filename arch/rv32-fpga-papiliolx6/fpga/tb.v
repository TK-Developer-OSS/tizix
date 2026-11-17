`timescale 1ns/1ps
// test bench of tizix_top: a 10MHz clock, RAM (behind the XBUS, 8MB of SDRAM), UART (115200 baud) input and output.
//   +HEX=ram.hex  : place the memory image (kernel + disk) with $readmemh
//   +IN=file      : the input sent to UART RX (+GAP ns between characters. Byte 0xFF is a 200ms pause)
//   +RAW          : send +IN as binary (0xFF is not a pause either). +IN2=file is sent as text with pauses, +WAIT2 ms later
//   +WAIT=n       : the wait before the input starts (milliseconds, default 300)
//   +MS=n         : simulation time (milliseconds)
module tb;
  reg clk = 0, rstn = 0;
  always #50 clk = ~clk;            // 10MHz

  wire txd;
  reg  rxd = 1;
  wire [31:0] adr, dato; wire we, stb, cyc; wire [3:0] sel;
  wire [31:0] dati; wire ack;

  tizix_top dut(.clk_i(clk), .rstn_i(rstn), .uart0_txd_o(txd), .uart0_rxd_i(rxd),
    .xbus_adr_o(adr), .xbus_dat_o(dato), .xbus_we_o(we), .xbus_sel_o(sel),
    .xbus_stb_o(stb), .xbus_cyc_o(cyc), .xbus_dat_i(dati), .xbus_ack_i(ack), .xbus_err_i(1'b0));

  // ---- RAM: 8MB from 0x80000000 (same geometry as the Micron MT48LC4M16). An SDRAM (behavioural model) behind a PSRAM-style controller ----
  wire sd_cke, sd_cs_n, sd_ras_n, sd_cas_n, sd_we_n, sd_oe, sdr_ready;
  wire [1:0] sd_ba, sd_dqm; wire [12:0] sd_a; wire [15:0] sd_dq_o, sd_dq_i;
  wire inram = (adr[31:23] == 9'h100);          // 8MB from 0x80000000
  sdram_psram #(.CLK_HZ(10000000)) ctl(.cap(3'd3), .clk(clk), .rst(!rstn),
    .cyc(cyc && inram), .stb(stb), .we(we), .sel(sel), .adr(adr[24:0]), .dati(dato), .dato(dati), .ack(ack),
    .sd_cke(sd_cke), .sd_cs_n(sd_cs_n), .sd_ras_n(sd_ras_n), .sd_cas_n(sd_cas_n), .sd_we_n(sd_we_n),
    .sd_ba(sd_ba), .sd_a(sd_a), .sd_dqm(sd_dqm), .sd_dq_o(sd_dq_o), .sd_dq_oe(sd_oe), .sd_dq_i(sd_dq_i), .ready(sdr_ready));
  sdram_model #(.CLK_HZ(10000000)) sdr(.clk(clk), .cke(sd_cke), .cs_n(sd_cs_n), .ras_n(sd_ras_n), .cas_n(sd_cas_n),
    .we_n(sd_we_n), .ba(sd_ba), .a(sd_a), .dqm(sd_dqm), .dq_i(sd_dq_o), .dq_oe(sd_oe), .dq_o(sd_dq_i));
  always @(posedge clk) if (cyc && stb && !inram) begin
    $display("\n[tb] XBUS access outside RAM: adr=%08x we=%b", adr, we);
    $finish;
  end

  // put the memory image straight into the SDRAM model (a back door, unrelated to initialisation)
  reg [31:0] img [0:2097151];
  integer k;
  task load_image(input [1023:0] f);
    begin
      $readmemh(f, img);
      for (k = 0; k < 2097152; k = k + 1) begin
        sdr.mem[2*k]   = img[k][15:0];
        sdr.mem[2*k+1] = img[k][31:16];
      end
    end
  endtask

  // ---- UART receive (turn TX into characters) ----
  localparam BIT = 8681;            // 115200 baud(ns)
  reg [7:0] b; integer i;
  initial forever begin
    @(negedge txd); #(BIT/2);
    for (i = 0; i < 8; i = i + 1) begin #(BIT); b[i] = txd; end
    #(BIT);
    $write("%c", b); $fflush;
  end

  // ---- UART transmit (send the input file) ----
  integer fd, c, gap, ms, waitms, wait2;
  reg [1023:0] hexf, inf, inf2;
  reg raw;
  // raw=0: byte 0xFF is a 200ms pause (for text). raw=1: send every byte as it is (binary, for the loader)
  task send_file(input integer f, input r);
    integer ch, j;
    begin
      ch = $fgetc(f);
      while (ch != -1) begin
        if (!r && ch == 255) begin #(200000000); ch = $fgetc(f); end
        else begin
          rxd = 0; #(BIT);
          for (j = 0; j < 8; j = j + 1) begin rxd = ch[j]; #(BIT); end
          rxd = 1; #(BIT + gap);
          ch = $fgetc(f);
        end
      end
    end
  endtask
  initial begin
    if ($value$plusargs("HEX=%s", hexf)) load_image(hexf);
    if (!$value$plusargs("MS=%d", ms)) ms = 100;
    if (!$value$plusargs("GAP=%d", gap)) gap = 0;
    if (!$value$plusargs("WAIT=%d", waitms)) waitms = 300;
    if (!$value$plusargs("WAIT2=%d", wait2)) wait2 = 300;
    raw = $test$plusargs("RAW");
    #1000 rstn = 1;
    if ($value$plusargs("IN=%s", inf)) begin
      fd = $fopen(inf, "rb");
      #(waitms * 1000000);          // wait for boot (default 300ms; with the loader, the time the SDRAM is being filled with 0)
      send_file(fd, raw);
      if ($value$plusargs("IN2=%s", inf2)) begin
        fd = $fopen(inf2, "rb");
        #(wait2 * 1000000);         // wait until the first stream (the loader) is done and the kernel is up
        send_file(fd, 0);
      end
    end
  end
  initial begin #(ms * 1000000); $display("\n[tb] end, sdram errors=%0d", sdr.errs); $finish; end
endmodule
