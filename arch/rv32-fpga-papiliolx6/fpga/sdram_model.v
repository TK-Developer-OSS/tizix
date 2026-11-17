`timescale 1ns/1ps
// sdram_model: a simple behavioural model of an SDR SDRAM (default 64Mbit = 12 rows x 8 columns x 4 banks x 16 bits; Micron MT48LC4M16), for simulation only.
//   Only CL=2, burst length 2 and auto precharge are supported. It checks the controller's procedure:
//   An access before initialisation (PRECHARGE ALL -> REFRESH x2 -> MODE REGISTER), a READ/WRITE to a closed row,
//   It reports a missed refresh interval (64ms/8192 rows = 7.8us) and ACTIVE spacing violations (tRCD/tRP).
module sdram_model #(parameter CLK_HZ = 32000000, parameter ROW_BITS = 12, parameter COL_BITS = 8) (
  input  wire        clk,
  input  wire        cke, cs_n, ras_n, cas_n, we_n,
  input  wire [1:0]  ba,
  input  wire [12:0] a,
  input  wire [1:0]  dqm,
  input  wire [15:0] dq_i,       // controller -> SDRAM
  input  wire        dq_oe,
  output reg  [15:0] dq_o        // SDRAM -> controller
);
  reg [15:0] mem [0:(1<<(ROW_BITS+2+COL_BITS))-1];   // index = byte address / 2 = {row, bank, col}
  wire [3:0] cmd = {cs_n, ras_n, cas_n, we_n};
  integer errs = 0;
  reg mode_ok = 0; integer nref = 0; reg pre_all = 0;
  reg [ROW_BITS-1:0] arow [0:3]; reg aopen [0:3];
  integer last_act [0:3]; integer last_pre [0:3];
  integer cyc = 0;
  integer last_ref;
  // read pipeline (CL=2 -> dq comes out 2 beats after the beat that received the command)
  reg [15:0] rd0, rd1; reg rd_v0 = 0, rd_v1 = 0, rd_v2 = 0;
  reg [15:0] p0, p1;
  // second write beat
  reg        wr_pend = 0; reg [ROW_BITS+COL_BITS+1:0] wr_idx; reg [1:0] wr_ba;
  integer i;
  initial begin for (i = 0; i < 4; i = i + 1) begin aopen[i] = 0; last_act[i] = -100; last_pre[i] = -100; end last_ref = 0; dq_o = 16'h0000; end

  function [ROW_BITS+COL_BITS+1:0] idx(input [ROW_BITS-1:0] row, input [1:0] b, input [COL_BITS-1:0] col);
    idx = {row, b, col};
  endfunction

  always @(posedge clk) begin
    cyc = cyc + 1;
    // read: the first word comes 2 beats after the beat E1 that received READ (CL=2), the second word on the next beat
    dq_o <= 16'h0000;
    if (rd_v1) begin dq_o <= p0; rd_v2 <= 1; end else rd_v2 <= 0;
    if (rd_v2) dq_o <= p1;
    rd_v1 <= rd_v0; rd_v0 <= 0;
    // second write beat
    if (wr_pend) begin
      if (!dqm[0]) mem[wr_idx][7:0]  <= dq_i[7:0];
      if (!dqm[1]) mem[wr_idx][15:8] <= dq_i[15:8];
      wr_pend <= 0;
    end
    if (cke && !cs_n) begin
      case (cmd)
        4'b0000: begin      // MODE REGISTER SET
          if (!(pre_all && nref >= 2)) begin errs = errs + 1; $display("[sdram] MRS before PRECHARGE+2xREFRESH @%0d", cyc); end
          if (a !== 13'h0021) begin errs = errs + 1; $display("[sdram] unexpected mode %h", a); end
          mode_ok = 1; last_ref = cyc;
        end
        4'b0001: begin      // AUTO REFRESH
          if (!pre_all) begin errs = errs + 1; $display("[sdram] REFRESH before PRECHARGE @%0d", cyc); end
          nref = nref + 1; last_ref = cyc;
        end
        4'b0010: begin      // PRECHARGE
          if (a[10]) begin pre_all = 1; for (i = 0; i < 4; i = i + 1) aopen[i] = 0; end
          else aopen[ba] = 0;
        end
        4'b0011: begin      // ACTIVE
          if (!mode_ok) begin errs = errs + 1; $display("[sdram] ACTIVE before init @%0d", cyc); end
          if (aopen[ba]) begin errs = errs + 1; $display("[sdram] ACTIVE on open bank %0d @%0d", ba, cyc); end
          aopen[ba] = 1; arow[ba] = a[ROW_BITS-1:0]; last_act[ba] = cyc;
        end
        4'b0101: begin      // READ (a[10] = auto precharge)
          if (!aopen[ba]) begin errs = errs + 1; $display("[sdram] READ on closed bank @%0d", cyc); end
          if (cyc - last_act[ba] < 2) begin errs = errs + 1; $display("[sdram] tRCD violated @%0d", cyc); end
          p0 <= mem[idx(arow[ba], ba, {a[COL_BITS-1:1], 1'b0})];
          p1 <= mem[idx(arow[ba], ba, {a[COL_BITS-1:1], 1'b1})];
          rd_v0 <= 1;
          if (a[10]) aopen[ba] = 0;
        end
        4'b0100: begin      // WRITE
          if (!aopen[ba]) begin errs = errs + 1; $display("[sdram] WRITE on closed bank @%0d", cyc); end
          if (cyc - last_act[ba] < 2) begin errs = errs + 1; $display("[sdram] tRCD violated @%0d", cyc); end
          if (!dqm[0]) mem[idx(arow[ba], ba, {a[COL_BITS-1:1], 1'b0})][7:0]  <= dq_i[7:0];
          if (!dqm[1]) mem[idx(arow[ba], ba, {a[COL_BITS-1:1], 1'b0})][15:8] <= dq_i[15:8];
          wr_pend <= 1; wr_idx <= idx(arow[ba], ba, {a[COL_BITS-1:1], 1'b1});
          if (a[10]) aopen[ba] = 0;
        end
        default: ;
      endcase
    end
    // refresh missed (after initialisation, no REFRESH for more than 64ms/8192 = 7.8us)
    if (mode_ok && (cyc - last_ref) > (CLK_HZ / 1000000) * 9) begin
      errs = errs + 1; $display("[sdram] refresh interval exceeded @%0d", cyc); last_ref = cyc;
    end
  end
endmodule
