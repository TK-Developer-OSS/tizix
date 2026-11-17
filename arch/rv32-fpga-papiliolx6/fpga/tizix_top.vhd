-- tizix_top: NEORV32 + UART0 + CLINT, the RAM is behind the external bus (XBUS).
--   The CPU starts at 0x80000000 (same address as rv32-qemu). What is behind the XBUS is the behavioural RAM of
--   tb.v in simulation, and the SDRAM controller on the board.
library ieee;
use ieee.std_logic_1164.all;
library neorv32;
use neorv32.neorv32_package.all;

entity tizix_top is
  generic (
    CLOCK_FREQUENCY : natural := 10000000;
    LOADER          : boolean := false   -- true: boot from the boot loader in IMEM (ROM) and receive the kernel over the UART
  );
  port (
    clk_i       : in  std_ulogic;
    rstn_i      : in  std_ulogic;
    uart0_txd_o : out std_ulogic;
    uart0_rxd_i : in  std_ulogic;
    xbus_adr_o  : out std_ulogic_vector(31 downto 0);
    xbus_dat_o  : out std_ulogic_vector(31 downto 0);
    xbus_we_o   : out std_ulogic;
    xbus_sel_o  : out std_ulogic_vector(3 downto 0);
    xbus_stb_o  : out std_ulogic;
    xbus_cyc_o  : out std_ulogic;
    xbus_dat_i  : in  std_ulogic_vector(31 downto 0);
    xbus_ack_i  : in  std_ulogic;
    xbus_err_i  : in  std_ulogic
  );
end entity;

architecture rtl of tizix_top is
  -- BOOT_MODE_SELECT: 1 = from 0x80000000 (simulation puts the memory in through a back door), 2 = the loader in IMEM (ROM)
  function boot_f(l : boolean) return natural is
  begin
    if l then return 2; else return 1; end if;
  end function;
begin
  core: neorv32_top
  generic map (
    CLOCK_FREQUENCY  => CLOCK_FREQUENCY,
    BOOT_MODE_SELECT => boot_f(LOADER),
    BOOT_ADDR_CUSTOM => x"80000000",
    RISCV_ISA_M      => true,
    RISCV_ISA_Zicntr => true,
    IMEM_EN          => LOADER,
    IMEM_SIZE        => 4096,
    DMEM_EN          => LOADER,
    DMEM_BASE        => x"A0000000",
    DMEM_SIZE        => 4096,
    XBUS_EN          => true,
    XBUS_TIMEOUT     => 0,
    IO_CLINT_EN      => true,
    IO_UART0_EN      => true,
    IO_UART0_RX_FIFO => 16,
    IO_UART0_TX_FIFO => 16
  )
  port map (
    clk_i       => clk_i,
    rstn_i      => rstn_i,
    uart0_txd_o => uart0_txd_o,
    uart0_rxd_i => uart0_rxd_i,
    xbus_adr_o  => xbus_adr_o,
    xbus_dat_o  => xbus_dat_o,
    xbus_we_o   => xbus_we_o,
    xbus_sel_o  => xbus_sel_o,
    xbus_stb_o  => xbus_stb_o,
    xbus_cyc_o  => xbus_cyc_o,
    xbus_dat_i  => xbus_dat_i,
    xbus_ack_i  => xbus_ack_i,
    xbus_err_i  => xbus_err_i
  );
end architecture;
