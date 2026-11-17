// papilio_top: the top level of the Papilio Pro (Spartan-6 XC6SLX9).
//   32MHz -> PLL -> tizix_top (NEORV32 converted to Verilog with GHDL) -> XBUS -> sdram_psram -> SDRAM (MT48LC4M16)
//   The pins are in papilio_pro.ucf. Use the tizix_top converted with LOADER=true, CLOCK_FREQUENCY=CLK_HZ (fpga/build_ise.sh).
module papilio_top #(
  parameter CLK_HZ   = 64000000,        // system clock (Hz)
  parameter PLL_MULT = 20,              // VCO = 32MHz × PLL_MULT(400〜1000MHz)
  parameter PLL_DIV  = 10,              // CLK_HZ = VCO / PLL_DIV
  parameter SD_PHASE = 0                // phase of the SDRAM clock (degrees, advanced by the PLL; may be negative)
) (
  input  wire        clk32,
  input  wire        uart_rx,        // an input as seen from the FPGA (TX of FT2232 channel A)
  output wire        uart_tx,
  output wire        led,
  output wire        sd_clk,
  output wire        sd_cke,
  output wire        sd_cs_n,
  output wire        sd_ras_n,
  output wire        sd_cas_n,
  output wire        sd_we_n,
  output wire [1:0]  sd_ba,
  output wire [12:0] sd_a,
  output wire [1:0]  sd_dqm,
  inout  wire [15:0] sd_dq
);
  // clock: 32MHz -> PLL (VCO 640MHz) -> CLK_HZ. Reset is released only after the PLL has locked
  localparam CLK_MHZ = CLK_HZ / 1000000;
  wire clk, clk_sd, pll_fb, pll_out, pll_sd, pll_locked;
  PLL_BASE #(.BANDWIDTH("OPTIMIZED"), .CLKIN_PERIOD(31.25), .CLKFBOUT_MULT(PLL_MULT), .DIVCLK_DIVIDE(1),
             .CLKOUT0_DIVIDE(PLL_DIV), .CLKOUT0_DUTY_CYCLE(0.5),
             .CLKOUT1_DIVIDE(PLL_DIV), .CLKOUT1_DUTY_CYCLE(0.5), .CLKOUT1_PHASE(SD_PHASE), .COMPENSATION("INTERNAL"), .REF_JITTER(0.01)) pll (
    .CLKIN(clk32), .CLKFBIN(pll_fb), .CLKFBOUT(pll_fb), .CLKOUT0(pll_out),
    .CLKOUT1(pll_sd), .CLKOUT2(), .CLKOUT3(), .CLKOUT4(), .CLKOUT5(), .LOCKED(pll_locked), .RST(1'b0));
  BUFG bufg_clk (.I(pll_out), .O(clk));
  BUFG bufg_sd  (.I(pll_sd),  .O(clk_sd));

  // power-on reset: about 2ms (2^16 clocks) after the PLL locks
  reg [15:0] por = 16'h0000;
  wire rstn = por[15];
  always @(posedge clk) if (!pll_locked) por <= 16'h0000; else if (!rstn) por <= por + 1'b1;

  // CPU and XBUS
  wire [31:0] adr, dato, dati;
  wire        we, stb, cyc, ack;
  wire [3:0]  sel;
  wire        inram = (adr[31:23] == 9'h100);           // 8MB from 0x80000000
  wire        incsr = (adr[31:24] == 8'hF0);            // 0xF0000000: control register (bit2:0 = SDRAM capture beat)
  reg         err, csr_ack;
  reg  [2:0]  cap_r = 3'd2;
  wire        sd_ack;
  wire [31:0] sd_dati;
  always @(posedge clk) begin
    err     <= cyc && stb && !inram && !incsr;           // answer anything else so the CPU does not hang
    csr_ack <= cyc && stb && incsr;
    if (cyc && stb && incsr && we) cap_r <= dato[2:0];
  end
  assign ack  = sd_ack | csr_ack;
  assign dati = csr_ack ? {29'b0, cap_r} : sd_dati;

  tizix_top cpu (
    .clk_i(clk), .rstn_i(rstn),
    .uart0_txd_o(uart_tx), .uart0_rxd_i(uart_rx),
    .xbus_adr_o(adr), .xbus_dat_o(dato), .xbus_we_o(we), .xbus_sel_o(sel),
    .xbus_stb_o(stb), .xbus_cyc_o(cyc),
    .xbus_dat_i(dati), .xbus_ack_i(ack), .xbus_err_i(err));

  // SDRAM
  wire        sd_oe, ready;
  wire [15:0] sd_dq_o, sd_dq_i;
  // SDRAM (MT48LC4M16) timing: tRCD/tRP 20ns and tRFC 66ns converted to clock counts
  localparam NS_RCD = (20 * CLK_MHZ + 999) / 1000;
  localparam NS_RFC = (66 * CLK_MHZ + 999) / 1000;
  sdram_psram #(.CLK_HZ(CLK_HZ), .ROW_BITS(12), .COL_BITS(8),
                .T_RCD(NS_RCD), .T_RP(NS_RCD), .T_RFC(NS_RFC)) ctl (
    .cap(cap_r), .clk(clk), .rst(!rstn),
    .cyc(cyc && inram), .stb(stb), .we(we), .sel(sel), .adr(adr[24:0]),
    .dati(dato), .dato(sd_dati), .ack(sd_ack),
    .sd_cke(sd_cke), .sd_cs_n(sd_cs_n), .sd_ras_n(sd_ras_n), .sd_cas_n(sd_cas_n), .sd_we_n(sd_we_n),
    .sd_ba(sd_ba), .sd_a(sd_a), .sd_dqm(sd_dqm),
    .sd_dq_o(sd_dq_o), .sd_dq_oe(sd_oe), .sd_dq_i(sd_dq_i), .ready(ready));

  assign sd_dq   = sd_oe ? sd_dq_o : 16'bz;
  assign sd_dq_i = sd_dq;

  // SDRAM clock: in phase with the clock inside the FPGA (output with an ODDR2)
  ODDR2 #(.DDR_ALIGNMENT("NONE"), .INIT(1'b0), .SRTYPE("SYNC")) sdclk_oddr (
    .Q(sd_clk), .C0(clk_sd), .C1(~clk_sd), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

  assign led = ready;               // lit once the SDRAM initialisation is done
endmodule
