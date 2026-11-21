// Testbench: the real picorv32 + cache + spimemio RTL from fpga/, glued together
// the same way pano_top.v does it, running the firmware's real start.s and
// spiflash.c against a behavioural M25P80.
`timescale 1ns / 1ps
`default_nettype none
module tb;
    parameter integer BITLEN = 700;      // bytes written to the bitstream region
    parameter integer FWLEN  = 300;      // bytes written to the firmware region
    integer MAXCYCLES = 5000000;                 // override with +maxcycles=N

    reg clk = 0;  always #20 clk = ~clk;          // 25 MHz
    reg rst = 1;                                  // cache reset (active high)
    reg rst_rv = 0;                               // cpu / spimemio resetn

    // ---- CPU bus -------------------------------------------------------
    wire        mem_valid, mem_instr;
    wire        mem_ready;
    wire [31:0] mem_addr, mem_wdata, mem_la_addr;
    wire [3:0]  mem_wstrb;
    wire [31:0] mem_rdata;
    reg         cpu_irq = 0;
    wire        cpu_trap;

    // ---- address decode: copied from pano_top.v --------------------------
    wire la_addr_in_ram    = (mem_la_addr >= 32'hFFFF0000);
    wire la_addr_in_gpio   = (mem_la_addr >= 32'h03000000) && (mem_la_addr < 32'h03000100);
    wire la_addr_in_ddr    = (mem_la_addr >= 32'h0C000000) && (mem_la_addr < 32'h0D000000);
    wire la_addr_in_spi    = (mem_la_addr >= 32'h0E000000) && (mem_la_addr < 32'h0E020000);
    wire la_addr_in_spicfg = (mem_la_addr == 32'h0E020000);
    reg addr_in_ram, addr_in_gpio, addr_in_ddr, addr_in_spi, addr_in_spicfg;
    always @(posedge clk) begin
        addr_in_ram    <= la_addr_in_ram;
        addr_in_gpio   <= la_addr_in_gpio;
        addr_in_ddr    <= la_addr_in_ddr;
        addr_in_spi    <= la_addr_in_spi;
        addr_in_spicfg <= la_addr_in_spicfg;
    end

    wire [3:0]  spicfg_we = (addr_in_spicfg && mem_valid) ? mem_wstrb : 4'b0000;
    wire [31:0] spicfg_rdata;

    wire ram_valid    = mem_valid && !mem_ready && addr_in_ram;
    wire gpio_valid   = mem_valid && addr_in_gpio;
    wire ddr_valid    = mem_valid && addr_in_ddr;
    wire spi_valid    = mem_valid && addr_in_spi;
    wire spicfg_valid = mem_valid && addr_in_spicfg;
    wire general_valid = mem_valid && !mem_ready && !addr_in_ddr && !addr_in_spi;
    reg default_ready = 0;
    always @(posedge clk) default_ready <= general_valid;

    wire spi_ready;
    reg  ddr_ready_buf = 0;
    assign mem_ready = ddr_ready_buf || spi_ready || default_ready;

    // bus-fault detector, as in pano_top.v.  Without FAULT_FIX this is the
    // logic in the bitstream currently on the board (cfgreg not excluded).
    reg mem_valid_last = 0;
    always @(posedge clk) begin
        mem_valid_last <= mem_valid;
        if (mem_valid && !mem_valid_last &&
            !(ram_valid || spi_valid || gpio_valid || ddr_valid
`ifdef FAULT_FIX
              || spicfg_valid
`endif
             ))
            cpu_irq <= 1'b1;
        if (!rst_rv) cpu_irq <= 1'b0;
    end

    // ---- flash: cache -> spimemio -> M25P80 model ----------------------
    wire [16:0] spimem_addr;
    wire        spimem_ready, spimem_valid;
    wire [31:0] spimem_rdata, spi_rdata;
    wire flash_csb, flash_clk, flash_mosi, flash_miso;

    cache cache(
        .clk(clk), .rst(rst),
        .sys_addr(mem_addr[16:0]), .sys_rdata(spi_rdata),
        .sys_valid(spi_valid), .sys_ready(spi_ready),
        .mem_addr(spimem_addr), .mem_rdata(spimem_rdata),
        .mem_valid(spimem_valid), .mem_ready(spimem_ready));

    spimemio spimemio (
        .clk(clk), .resetn(rst_rv),
        .valid(spimem_valid), .ready(spimem_ready),
        .addr({4'b0, 3'b110, spimem_addr}), .rdata(spimem_rdata),
        .flash_csb(flash_csb), .flash_clk(flash_clk),
        .flash_io0_oe(), .flash_io1_oe(), .flash_io2_oe(), .flash_io3_oe(),
        .flash_io0_do(flash_mosi), .flash_io1_do(), .flash_io2_do(), .flash_io3_do(),
        .flash_io0_di(1'b0), .flash_io1_di(flash_miso),
        .flash_io2_di(1'b0), .flash_io3_di(1'b0),
        .cfgreg_we(spicfg_we), .cfgreg_di(mem_wdata), .cfgreg_do(spicfg_rdata));

    m25p80_model flash(.csb(flash_csb), .clk(flash_clk), .di(flash_mosi), .dout(flash_miso));

    // ---- internal RAM (stack), as in pano_top.v ---------------------------
    wire [31:0] ram_rdata;
    picosoc_mem #(.WORDS(2048)) memory (
        .clk(clk), .wen(ram_valid ? mem_wstrb : 4'b0),
        .addr({11'b0, mem_addr[12:2]}), .wdata(mem_wdata), .rdata(ram_rdata));

    // ---- DDR: the real bridge from fpga/, glued as in pano_top.v ----------
    // The bridge runs on the 100 MHz clock the 25 MHz CPU clock is divided
    // from (same DCM, edges aligned).  It keeps ddr_ready up until it has seen
    // ddr_valid low, which matters for code that executes from DDR: see
    // the "compressed code" run in run.py.
    reg clk_100 = 1;  always #5 clk_100 = ~clk_100;      // rises with clk
    reg clk_100_90 = 0;  always @(clk_100) clk_100_90 <= #2.5 clk_100;
    reg ddr_rst = 1;  initial #400 ddr_rst = 0;

    wire [31:0] ddr_rdata;
    wire        ddr_ready;
    wire [31:0] user_input_data, user_output_data;
    wire [22:0] user_input_address;
    wire [2:0]  user_command_register;
    wire [3:0]  user_data_mask;
    wire user_data_valid, user_cmd_ack, burst_done, init_done, ar_done, auto_ref_req;

    reg [31:0] ddr_rdata_buf;
    always @(posedge clk) begin
        ddr_rdata_buf <= ddr_rdata;
        ddr_ready_buf <= ddr_ready;
    end

    mig_picorv_bridge mig_picorv_bridge(
        .clk0(clk_100), .clk90(clk_100_90), .sys_rst180(ddr_rst),
        .ddr_addr(mem_addr[23:0]), .ddr_wdata(mem_wdata), .ddr_rdata(ddr_rdata),
        .ddr_wstrb(mem_wstrb), .ddr_valid(ddr_valid), .ddr_ready(ddr_ready),
        .auto_refresh_req(auto_ref_req),
        .user_input_data(user_input_data), .user_output_data(user_output_data),
        .user_data_valid(user_data_valid), .user_input_address(user_input_address),
        .user_command_register(user_command_register), .user_cmd_ack(user_cmd_ack),
        .user_data_mask(user_data_mask), .burst_done(burst_done),
        .init_done(init_done), .ar_done(ar_done));

    mig_user_model mig(
        .clk0(clk_100), .clk90(clk_100_90), .rst(ddr_rst),
        .user_input_data(user_input_data), .user_output_data(user_output_data),
        .user_data_valid(user_data_valid), .user_input_address(user_input_address),
        .user_command_register(user_command_register), .user_cmd_ack(user_cmd_ack),
        .user_data_mask(user_data_mask), .burst_done(burst_done),
        .init_done(init_done), .ar_done(ar_done), .auto_refresh_req(auto_ref_req));

    // ---- GPIO: LED register at +4, sim-only report register at +8 ----------
    reg [2:0] led = 0;
    integer led_writes = 0;
    reg [31:0] report [0:15];
    integer n_report = 0;
    reg seen_working = 0;
    always @(posedge clk) if (gpio_valid && |mem_wstrb && default_ready) begin
        if (mem_addr[7:0] == 8'h04) begin
            led <= mem_wdata[2:0]; led_writes = led_writes + 1;
            if (mem_wdata[2:0] == 3'd3) seen_working <= 1;
            $display("[%0t] LED <= %0d", $time, mem_wdata[2:0]);
        end
        if (mem_addr[7:0] == 8'h08) begin
            report[n_report] = mem_wdata; n_report = n_report + 1;
            $display("[%0t] REPORT %08x", $time, mem_wdata);
        end
    end

    assign mem_rdata =
        addr_in_ram    ? ram_rdata :
        addr_in_ddr    ? ddr_rdata_buf :
        addr_in_gpio   ? 32'h0 :
        addr_in_spi    ? spi_rdata :
        addr_in_spicfg ? spicfg_rdata : 32'hFFFFFFFF;

    picorv32 #(
        .STACKADDR(32'hfffffffc), .PROGADDR_RESET(32'h0e000000),
        .ENABLE_IRQ(1), .ENABLE_IRQ_QREGS(0), .ENABLE_IRQ_TIMER(0),
        .COMPRESSED_ISA(1), .PROGADDR_IRQ(32'h0e000008),
        .MASKED_IRQ(32'hfffffffe), .LATCHED_IRQ(32'hffffffff)
    ) cpu (
        .clk(clk), .resetn(rst_rv),
        .mem_valid(mem_valid), .mem_instr(mem_instr), .mem_ready(mem_ready),
        .mem_addr(mem_addr), .mem_wdata(mem_wdata), .mem_wstrb(mem_wstrb),
        .mem_rdata(mem_rdata), .mem_la_addr(mem_la_addr),
        .irq({31'b0, cpu_irq}), .trap(cpu_trap));

    // ---- stimulus / checking -------------------------------------------
    function [7:0] pat_bit; input integer i; pat_bit = (i + 8'h5A) ^ (i >> 8); endfunction
    function [7:0] pat_fw;  input integer i; pat_fw  = (~i) ^ 8'h3C ^ (i >> 8); endfunction

    integer i, errors, cycles;
    reg [1023:0] fwhex;
    reg preload;
    initial begin
        for (i = 0; i < 1048576; i = i + 1) flash.mem[i] = 8'hFF;
        for (i = 0; i < 8192; i = i + 1) flash.mem[i] = i[7:0] ^ 8'hA5;     // "old bitstream"
        if (!$value$plusargs("fw=%s", fwhex)) fwhex = "tb_fw.hex";
        if (!$value$plusargs("maxcycles=%d", MAXCYCLES)) MAXCYCLES = 5000000;
        $readmemh(fwhex, flash.mem, 20'hC0000);
        // +preload: flash already holds exactly what the update will write
        // (apart from the firmware's own code) - nothing should be erased.
        preload = $test$plusargs("preload_bit");
        if (preload) begin
            for (i = 0; i < BITLEN; i = i + 1) flash.mem[i] = pat_bit(i);
            for (i = BITLEN; i < 65536; i = i + 1) flash.mem[i] = 8'hFF;
        end
        repeat (40) @(posedge clk);  rst = 0;
        repeat (300) @(posedge clk); rst_rv = 1;
    end

    task finish_run; input [255:0] why; begin
        errors = 0;
        $display("---- %0s after %0d cycles ----", why, cycles);
        $display("flash model: RDID=%0d WREN=%0d SE=%0d PP=%0d READ=%0d ignored(busy)=%0d ignored(no WEL)=%0d",
                 flash.n_rdid, flash.n_wren, flash.n_se, flash.n_pp, flash.n_read,
                 flash.n_ignored_busy, flash.n_ignored_nowel);
        $display("LED=%0d seen_yellow=%0d cpu_irq_latched=%0d config_en=%0d", led, seen_working, cpu_irq, spimemio.config_en);
        $display("cpu_trap=%0d pc=%08x ddr: reads=%0d writes=%0d refreshes=%0d", cpu_trap, cpu.reg_pc, mig.n_read, mig.n_write, mig.n_refresh);
        if ($test$plusargs("check_flash")) begin
            for (i = 0; i < BITLEN; i = i + 1)
                if (flash.mem[i] !== pat_bit(i)) begin errors = errors + 1; if (errors < 5) $display("  bit region mismatch @%0d: %02x != %02x", i, flash.mem[i], pat_bit(i)); end
            for (i = BITLEN; i < 65536; i = i + 1)
                if (flash.mem[i] !== 8'hFF) begin errors = errors + 1; if (errors < 5) $display("  sector 0 tail not erased @%0d", i); end
            for (i = 65536; i < 20'hC0000; i = i + 1)
                if (flash.mem[i] !== 8'hFF) begin errors = errors + 1; if (errors < 5) $display("  untouched area changed @%0d", i); end
            for (i = 0; i < FWLEN; i = i + 1)
                if (flash.mem[20'hC0000 + i] !== pat_fw(i)) begin errors = errors + 1; if (errors < 5) $display("  fw region mismatch @%0d: %02x != %02x", i, flash.mem[20'hC0000+i], pat_fw(i)); end
            for (i = FWLEN; i < 65536; i = i + 1)
                if (flash.mem[20'hC0000 + i] !== 8'hFF) begin errors = errors + 1; if (errors < 5) $display("  sector 12 tail not erased @%0d", i); end
            $display("flash content check: %0d mismatching bytes", errors);
        end
        $finish;
    end endtask

    integer settle = -1;
    initial cycles = 0;
    always @(posedge clk) begin
        cycles <= cycles + 1;
        if (cycles == MAXCYCLES) finish_run("TIMEOUT");
        if (cpu_trap && settle < 0) begin $display("[%0t] CPU TRAP (halted) pc=%08x", $time, cpu.reg_pc); settle <= 20; end
        if (settle > 0) settle <= settle - 1;
        if (settle == 0) finish_run("DONE");
        if (settle < 0 && seen_working && (led == 3'd1 || led == 3'd2)) settle <= 2000;
        if (settle < 0 && n_report > 0 &&
            (report[n_report-1][31:20] == 12'hBAD || report[n_report-1][31:16] == 16'hDEAD ||
             report[n_report-1] == 32'h0D0E0000))
            settle <= 200;
    end
endmodule
`default_nettype wire
