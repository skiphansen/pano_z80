`timescale 1ns / 1ps

// Test of the sound and Z80 speed logic: fpga/audio_beeper.v (the codec's
// I2S stream) and the parts of fpga/cpm_io.v that go with it (port 32h, the
// sound control and Z80 speed registers, the Z80 clock enable).
// Prints "PASS" or "FAIL: ..." lines; the Makefile fails on any FAIL.

module tb;
    reg clk = 0;
    always #20 clk = !clk;          // 25 MHz
    reg reset = 1;
    integer fails = 0;

    task check(input cond, input [8*80-1:0] what);
        begin
            if (cond) $display("PASS %0s", what);
            else begin $display("FAIL: %0s", what); fails = fails + 1; end
        end
    endtask

    // ------------------------------------------------------------------
    // cpm_io, driven the way the Z80 and the RISC V drive it
    reg z80_iord = 0, z80_iowr = 0, z80_reset = 0;
    reg [7:0] z80adr = 0, z80do = 0;
    wire [7:0] z80di;
    wire z80_io_ready;
    reg io_valid = 0, rv_wstr = 0;
    reg [23:0] rv_wdata = 0;
    reg [3:0] rv_adr = 0;
    wire [23:0] rv_rdata;
    wire [23:0] fg, bg;
    wire beep, z80_cen;

    cpm_io io(
        .clk(clk), .reset(reset),
        .z80_iord(z80_iord), .z80_iowr(z80_iowr), .z80adr(z80adr),
        .z80di(z80di), .z80do(z80do), .z80_io_ready(z80_io_ready),
        .z80hlt(1'b0), .z80_reset(z80_reset),
        .io_valid(io_valid), .rv_wdata(rv_wdata), .rv_adr(rv_adr),
        .rv_wstr(rv_wstr), .rv_rdata(rv_rdata),
        .font_fg_color(fg), .font_bg_color(bg),
        .beep(beep), .z80_cen(z80_cen));

    task rv_write(input [3:0] a, input [23:0] d);
        begin
            @(negedge clk); io_valid = 1; rv_wstr = 1; rv_adr = a; rv_wdata = d;
            @(negedge clk); io_valid = 0; rv_wstr = 0;
        end
    endtask
    task rv_read(input [3:0] a, output [23:0] d);
        begin
            @(negedge clk); io_valid = 1; rv_wstr = 0; rv_adr = a;
            @(negedge clk); io_valid = 0; d = rv_rdata;
        end
    endtask
    // An OUT: hold IOWR until the I/O is ready, as WAIT_n makes the Z80 do
    task z80_out(input [7:0] port, input [7:0] d, output integer clocks);
        begin
            @(negedge clk); z80adr = port; z80do = d; z80_iowr = 1; clocks = 0;
            while (!z80_io_ready && clocks < 1000) begin
                @(negedge clk); clocks = clocks + 1;
            end
            @(negedge clk); z80_iowr = 0;
            repeat (2) @(negedge clk);
        end
    endtask

    integer i, n, clocks;
    reg [23:0] d;

    // count enabled clocks over a window
    task count_cen(input integer window, output integer count);
        begin
            repeat (2) @(posedge clk);       // the new speed takes a clock
            count = 0;
            for (i = 0; i < window; i = i + 1) begin
                @(posedge clk); if (z80_cen) count = count + 1;
            end
        end
    endtask

    // ------------------------------------------------------------------
    // audio_beeper, with an I2S receiver checking what the codec would see
    reg on = 0;
    wire mclk, bclk, lrck, dacdat;
    audio_beeper beeper(.clk(clk), .reset(reset), .on(on),
                        .mclk(mclk), .bclk(bclk), .lrck(lrck), .dacdat(dacdat));

    // the receiver: on each rising edge of BCLK take a bit; bit 0 after an
    // LRCK edge is the delay slot, bits 1-16 the sample
    integer bitno = 0, frames = 0;
    reg last_lrck = 0;
    reg [15:0] rx, left, right;
    reg signed [15:0] lastleft = 0;
    integer nonzero = 0, flips = 0, maxabs = 0, badpair = 0;
    always @(posedge bclk) begin
        if (lrck != last_lrck) begin
            if (last_lrck == 0) left = rx; else begin
                right = rx; frames = frames + 1;
                if (left != right) badpair = badpair + 1;
                if (left != 0) nonzero = nonzero + 1;
                if ($signed(left) != 0 && lastleft != 0 && (($signed(left) < 0) != (lastleft < 0))) flips = flips + 1;
                if ($signed(left) > maxabs) maxabs = $signed(left);
                if (-$signed(left) > maxabs) maxabs = -$signed(left);
                if (left != 0) lastleft = left;
            end
            bitno = 0; rx = 0; last_lrck = lrck;
        end
        if (bitno >= 1 && bitno <= 16) rx = {rx[14:0], dacdat};
        bitno = bitno + 1;
    end

    // clock relations: MCLK edges per BCLK and per LRCK period, and that
    // LRCK and DACDAT change only while BCLK is low (on its falling edge)
    integer mclk_rises = 0, bclk_rises = 0, lrck_rises = 0, bad_edges = 0;
    reg pm = 0, pb = 0, pl = 0, pd = 0;
    always @(posedge clk) begin
        if (mclk && !pm) mclk_rises = mclk_rises + 1;
        if (bclk && !pb) bclk_rises = bclk_rises + 1;
        if (lrck && !pl) lrck_rises = lrck_rises + 1;
        if ((lrck != pl || dacdat != pd) && bclk) bad_edges = bad_edges + 1;
        pm = mclk; pb = bclk; pl = lrck; pd = dacdat;
    end

    // a tune: toggle the bit every tune_half clocks (0 = leave it alone)
    integer tune_half = 0, tune_count = 0;
    always @(posedge clk) begin
        if (tune_half != 0) begin
            tune_count = tune_count + 1;
            if (tune_count >= tune_half) begin tune_count = 0; on = !on; end
        end
        else tune_count = 0;
    end

    // tone period, from the module's own square wave
    integer tone_last = 0, tone_period = 0, t = 0;
    reg ptone = 0;
    always @(posedge clk) begin
        t = t + 1;
        if (beeper.tone && !ptone) begin
            if (tone_last != 0) tone_period = t - tone_last;
            tone_last = t;
        end
        ptone = beeper.tone;
    end

    initial begin
        repeat (5) @(negedge clk);
        reset = 0;

        // ---- Z80 speed
        count_cen(2500, n);
        check(n == 2500, "full speed after reset: every clock enabled");
        rv_write(15, 4);
        count_cen(25000, n);
        check(n == 4000, "4 MHz: 4000 of 25000 clocks enabled");
        rv_write(15, 1);
        count_cen(25000, n);
        check(n == 1000, "1 MHz: 1000 of 25000 clocks enabled");
        rv_write(15, 25);
        count_cen(2500, n);
        check(n == 2500, "25 MHz: every clock enabled");
        rv_write(15, 0);
        count_cen(2500, n);
        check(n == 2500, "0: every clock enabled");
        rv_write(15, 4);
        rv_read(15, d);
        check(d == 4, "speed register reads back");
        rv_write(15, 0);

        // ---- port 32h
        rv_write(14, 0);
        z80_out(8'h32, 8'h01, clocks);
        check(clocks < 5, "OUT (32h) completes in hardware, without the RISC V");
        rv_read(11, d);
        check(d[1:0] != 2'd1, "OUT (32h) is not passed to the RISC V");
        check(beep == 0, "beeper on but sound off: silent");
        rv_write(14, 1);
        @(negedge clk);
        check(beep == 1, "beeper on, sound on: beep");
        z80_out(8'h32, 8'h00, clocks);
        check(beep == 0, "OUT (32h),0: silent");
        z80_out(8'h32, 8'hfe, clocks);
        check(beep == 0, "only bit 0 counts");
        rv_write(14, 3);
        @(negedge clk);
        check(beep == 1, "the firmware's bell beeps");
        rv_write(14, 1);
        rv_read(14, d);
        check(d == 1, "sound register reads back");
        z80_out(8'h32, 8'h01, clocks);
        @(negedge clk); z80_reset = 1; @(negedge clk); z80_reset = 0;
        @(negedge clk);
        check(beep == 0, "a Z80 reset silences the beeper");
        @(negedge clk); z80adr = 8'h33; z80do = 8'h01; z80_iowr = 1;
        repeat (20) @(negedge clk);
        check(!z80_io_ready, "other ports wait for the RISC V");
        rv_read(11, d);
        check(d[1:0] == 2'd1, "other ports are passed to the RISC V");
        rv_read(9, d);                       // the RISC V takes it
        repeat (3) @(negedge clk);
        check(z80_io_ready && d[7:0] == 8'h01, "the RISC V reads the data and the Z80 goes on");
        z80_iowr = 0;
        repeat (4) @(negedge clk);

        // ---- I2S stream
        frames = 0; nonzero = 0;
        repeat (512 * 40) @(negedge clk);
        check(nonzero == 0, "beeper off: silence on the I2S lines");

        // a tune: the bit toggles at 1 kHz (half period 12500 clocks)
        mclk_rises = 0; bclk_rises = 0; lrck_rises = 0; bad_edges = 0;
        frames = 0; nonzero = 0; flips = 0; maxabs = 0; badpair = 0;
        tune_half = 12500;
        repeat (25000 * 40) @(negedge clk);           // 40 ms
        tune_half = 0; on = 0;
        // (the window is not a whole number of frames: allow one)
        check(mclk_rises - 256 * lrck_rises <= 256 && 256 * lrck_rises - mclk_rises <= 256, "MCLK = 256 x LRCK");
        check(bclk_rises - 64 * lrck_rises <= 64 && 64 * lrck_rises - bclk_rises <= 64, "64 BCLK per LRCK period");
        check(bad_edges == 0, "LRCK and DACDAT change on the falling edge of BCLK");
        check(badpair == 0, "both channels get the same sample");
        // 40 ms of 1 kHz: 80 sign changes; with the 3250 Hz tone it would be 260
        $display("     tune: %0d sign changes, peak %0d", flips, maxabs);
        check(flips >= 76 && flips <= 82, "a tune plays at the pitch the bit toggles at, without the tone");
        check(maxabs >= 3000 && maxabs <= 8192, "a tune: level about +-4096 (-18 dBFS), no overflow");
        check(beeper.held == 0, "a tune never counts as a held bell");

        // silence again, settling exactly at zero
        repeat (512 * 2000) @(negedge clk);
        nonzero = 0;
        repeat (512 * 20) @(negedge clk);
        check(nonzero == 0, "after a tune: settles to exact silence");

        // a bell: the bit stays on; after 10 ms it is the 3250 Hz tone
        on = 1;
        repeat (25000 * 10 + 512 * 4) @(negedge clk);
        check(beeper.held == 1, "the bit held on for 10 ms: a bell");
        flips = 0; maxabs = 0;
        repeat (512 * 1000) @(negedge clk);          // 20.5 ms
        check(tone_period == 7692, "tone period 7692 clocks = 3250 Hz");
        // 3250 Hz flips 133 times in 20.5 ms
        $display("     bell: %0d sign changes, peak %0d", flips, maxabs);
        check(flips >= 125 && flips <= 140, "a bell plays the 3250 Hz tone");
        check(maxabs >= 1000 && maxabs <= 8192, "a bell: level in range");
        on = 0;
        repeat (512 * 2000) @(negedge clk);
        nonzero = 0;
        repeat (512 * 20) @(negedge clk);
        check(nonzero == 0, "after a bell: silence");

        if (fails == 0) $display("ALL PASSED");
        else $display("%0d FAILED", fails);
        $finish;
    end
endmodule
