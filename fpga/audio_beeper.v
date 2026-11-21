`timescale 1ns / 1ps

// The ITT 3030's beeper, played through the Pano's WM8750 codec.
//
// On the ITT 3030 bit 0 of I/O port 32h switches the beeper.  Programs use it
// in two ways:
//   - the BIOS bell switches it on, waits, and switches it off;
//   - tunes (MELODIE.COM) toggle it at the frequency of the note.
// MAME (skeleton/itt3030.cpp) models the beeper as a 3250 Hz beep that the
// bit switches on and off.  That suits the bell, but makes a tune a 3250 Hz
// whine chopped at the note's frequency.  So this does both: while the bit
// keeps changing, the speaker follows the bit itself (a square wave at the
// note's pitch); once it has stayed on for HOLD_MS, it is a bell and gets the
// 3250 Hz tone.
//
// Each sample is the average of the signal over the sample period (so the
// edges of a note do not alias into roughness), then goes through a DC
// blocker (no thump when a tune starts or stops) and a gentle low-pass (a
// less harsh square wave).
//
// Everything runs from the 25 MHz clock: the codec's clocks are divided down
// from it, so no other clock is needed.
//
//   MCLK = 25 MHz / 2   = 12.5 MHz
//   BCLK = MCLK / 4     = 3.125 MHz
//   LRCK = MCLK / 256   = 48.83 kHz, 32 BCLK per channel
//
// The codec runs as an I2S slave in normal mode with MCLK = 256 fs (set up by
// the firmware over I2C, see fw/firmware/sound.c); it only needs MCLK to be
// 256 times LRCK, not a particular frequency.  The data is I2S: 16 bits, MSB
// first, starting one BCLK after the LRCK edge, changing on the falling edge
// of BCLK; both channels get the same sample.

module audio_beeper #(
    parameter CLK_HZ = 25000000,
    parameter TONE_HZ = 3250,
    parameter HOLD_MS = 10,
    parameter AMPLITUDE_LOG2 = 13       // signal level 2^13: about +-4096 (-18 dBFS)
                                        // after the DC blocker
) (
    input  wire clk,
    input  wire reset,
    input  wire on,             // the beeper bit (already synchronous to clk)
    output wire mclk,
    output wire bclk,
    output wire lrck,
    output wire dacdat
);

    // 9-bit divider: bit 0 = MCLK, bit 2 = BCLK, bit 8 = LRCK; one sample
    // period is 512 clocks
    reg [8:0] div;
    always @(posedge clk) begin
        if (reset)
            div <= 9'd0;
        else
            div <= div + 9'd1;
    end
    assign mclk = div[0];
    assign bclk = div[2];
    assign lrck = div[8];

    // The tone: a square wave, flipped every half period
    localparam HALF = CLK_HZ / TONE_HZ / 2;
    reg [15:0] tone_count;
    reg tone;
    always @(posedge clk) begin
        if (reset) begin
            tone_count <= 16'd0;
            tone <= 1'b0;
        end
        else if (tone_count == HALF - 1) begin
            tone_count <= 16'd0;
            tone <= !tone;
        end
        else
            tone_count <= tone_count + 16'd1;
    end

    // How long the bit has been on: a bell, or part of a tune?
    localparam HOLD = CLK_HZ / 1000 * HOLD_MS;
    reg [23:0] on_count;
    wire held = on_count >= HOLD;
    always @(posedge clk) begin
        if (reset || !on)
            on_count <= 24'd0;
        else if (!held)
            on_count <= on_count + 24'd1;
    end
    wire level = held ? tone : on;

    // Average over the sample period: count the clocks the level is high
    wire frame_end = div == 9'd511;
    reg [9:0] high_count;
    always @(posedge clk) begin
        if (reset || frame_end)
            high_count <= {9'd0, level};
        else
            high_count <= high_count + {9'd0, level};
    end

    // Once per sample: x = 0 .. 2^AMPLITUDE_LOG2, then the DC blocker
    //   hp += x - x_prev - hp/256      (about 30 Hz)
    // and the low-pass
    //   lp += (hp - lp) / 2            (about 7 kHz)
    // Both step by at least 1 so that they settle exactly at 0 in silence.
    // (high_count is 0 .. 512 = 2^9)
    wire signed [19:0] x = $signed({10'd0, high_count}) <<< (AMPLITUDE_LOG2 - 9);
    reg signed [19:0] x_prev, hp, lp;
    wire signed [19:0] hp_leak = (hp >>> 8) == 0 && hp > 0 ? 20'sd1 : (hp >>> 8);
    wire signed [19:0] hp_next = hp + x - x_prev - hp_leak;
    wire signed [19:0] lp_diff = hp_next - lp;
    wire signed [19:0] lp_step = (lp_diff >>> 1) == 0 ? lp_diff : (lp_diff >>> 1);
    wire signed [19:0] lp_next = lp + lp_step;
    always @(posedge clk) begin
        if (reset) begin
            x_prev <= 20'sd0;
            hp <= 20'sd0;
            lp <= 20'sd0;
        end
        else if (frame_end) begin
            x_prev <= x;
            hp <= hp_next;
            lp <= lp_next;
        end
    end
    // to 16 bits, saturating
    wire signed [15:0] level16 = lp > 20'sd32767 ? 16'sd32767 :
                                 lp < -20'sd32768 ? -16'sd32768 : lp[15:0];

    reg [15:0] sample;
    reg out;

    // Where the next BCLK falling edge is: div[2:0] == 3'b111 is the last
    // clock of a BCLK high phase.  div[7:3] is the bit number within the
    // channel (0-31) after that edge, div[8] the channel.  The sample is
    // taken at the start of each frame so a channel never gets half of one
    // sample and half of another.
    wire bclk_fall = div[2:0] == 3'b111;
    wire [4:0] next_bit = div[7:3] + 5'd1;
    always @(posedge clk) begin
        if (reset) begin
            sample <= 16'd0;
            out <= 1'b0;
        end
        else if (bclk_fall) begin
            if (div[8:3] == 6'b111111)
                sample <= level16;      // the left channel starts next
            // I2S: bit 0 of a channel is the last bit of the one before
            // (still zero), bits 1-16 are the sample, MSB first
            if (next_bit >= 5'd1 && next_bit <= 5'd16)
                out <= sample[5'd16 - next_bit];
            else
                out <= 1'b0;
        end
    end
    assign dacdat = out;

endmodule
