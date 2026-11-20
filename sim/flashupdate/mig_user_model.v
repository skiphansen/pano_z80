// Behavioural stand-in for the user interface of the Xilinx MIG DDR controller
// (fpga/mig), with just enough behaviour to drive the real
// fpga/mig_picorv_bridge.v: command acknowledge, a two word burst, read data
// valid, and the periodic auto refresh request.
//
// The cycle counts are plausible, not taken from the real controller.  What
// the simulation is after is the bridge's handshake with the CPU, and that is
// the real RTL.
`timescale 1ns / 1ps
`default_nettype none
module mig_user_model #(
    parameter integer REFRESH_CYCLES = 780      // one request every 7.8 us
) (
    input  wire        clk0,
    input  wire        clk90,
    input  wire        rst,
    input  wire [31:0] user_input_data,
    output reg  [31:0] user_output_data,
    output reg         user_data_valid,
    input  wire [22:0] user_input_address,
    input  wire [2:0]  user_command_register,
    output reg         user_cmd_ack,
    input  wire [3:0]  user_data_mask,
    input  wire        burst_done,
    output reg         init_done,
    output reg         ar_done,
    output reg         auto_refresh_req
);
    // One 32-bit word per CPU word: the bridge passes CPU address bits 23:1.
    reg [31:0] mem [0:524287];                  // 2 MB
    reg [22:0] addr_l;
    wire [18:0] idx = addr_l[19:1];

    localparam S_IDLE = 0, S_ACKWAIT = 1, S_BURST = 2, S_END = 3, S_REFRESH = 4;
    reg [2:0] state;
    reg [3:0] cnt;
    reg [3:0] since_ack;
    reg       is_write;
    integer   refresh_timer;
    integer   n_read, n_write, n_refresh;

    initial begin
        state = S_IDLE; user_cmd_ack = 0; user_data_valid = 0; init_done = 0;
        ar_done = 0; auto_refresh_req = 0; refresh_timer = 0; since_ack = 0;
        n_read = 0; n_write = 0; n_refresh = 0; cnt = 0; is_write = 0;
    end

    always @(negedge clk0) begin
        ar_done <= 1'b0;
        if (rst) begin
            state <= S_IDLE; user_cmd_ack <= 1'b0; init_done <= 1'b0;
            auto_refresh_req <= 1'b0; refresh_timer <= 0;
        end
        else begin
            init_done <= 1'b1;
            if (refresh_timer == REFRESH_CYCLES) begin
                refresh_timer <= 0;
                auto_refresh_req <= 1'b1;
            end
            else
                refresh_timer <= refresh_timer + 1;
            if (since_ack != 4'd15)
                since_ack <= since_ack + 4'd1;

            case (state)
                S_IDLE: begin
                    if (auto_refresh_req && user_command_register == 3'b000) begin
                        cnt <= 4'd6;
                        state <= S_REFRESH;
                    end
                    else if (user_command_register == 3'b100 || user_command_register == 3'b110) begin
                        addr_l <= user_input_address;
                        is_write <= (user_command_register == 3'b100);
                        cnt <= 4'd2;
                        state <= S_ACKWAIT;
                    end
                end
                S_ACKWAIT: begin
                    if (cnt == 0) begin
                        user_cmd_ack <= 1'b1;
                        since_ack <= 4'd0;
                        state <= S_BURST;
                        if (is_write) n_write = n_write + 1; else n_read = n_read + 1;
                    end
                    else
                        cnt <= cnt - 4'd1;
                end
                S_BURST: begin
                    if (burst_done) begin
                        cnt <= 4'd3;
                        state <= S_END;
                    end
                end
                S_END: begin
                    if (cnt == 0) begin
                        user_cmd_ack <= 1'b0;
                        state <= S_IDLE;
                    end
                    else
                        cnt <= cnt - 4'd1;
                end
                S_REFRESH: begin
                    if (cnt == 0) begin
                        auto_refresh_req <= 1'b0;
                        ar_done <= 1'b1;
                        n_refresh = n_refresh + 1;
                        state <= S_IDLE;
                    end
                    else
                        cnt <= cnt - 4'd1;
                end
            endcase
        end
    end

    // Data path, on clk90 like the real one
    always @(posedge clk90) begin
        user_data_valid <= 1'b0;
        if (user_cmd_ack && !is_write && (since_ack == 4'd7 || since_ack == 4'd8)) begin
            user_data_valid <= 1'b1;
            // first word of the burst is the data, the second is not used
            user_output_data <= (since_ack == 4'd7) ? mem[idx] : 32'hDEADBEEF;
        end
        if (user_cmd_ack && is_write) begin
            if (user_data_mask[0] === 1'b0) mem[idx][ 7: 0] <= user_input_data[ 7: 0];
            if (user_data_mask[1] === 1'b0) mem[idx][15: 8] <= user_input_data[15: 8];
            if (user_data_mask[2] === 1'b0) mem[idx][23:16] <= user_input_data[23:16];
            if (user_data_mask[3] === 1'b0) mem[idx][31:24] <= user_input_data[31:24];
        end
    end
endmodule
`default_nettype wire
