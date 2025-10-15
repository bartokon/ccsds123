`timescale 1ns/1ps

module top_tb;
`include "impl_params.v"

  localparam integer PERIOD = 10;
  localparam integer SAMPLE_BYTES = (D + 7) / 8;
  localparam integer TOTAL_SAMPLES = NX * NY * NZ;

  reg clk = 1'b0;
  reg aresetn = 1'b0;
  reg [PIPELINES*D-1:0] in_tdata = {PIPELINES*D{1'b0}};
  reg in_tvalid = 1'b0;

  wire in_tready;
  wire [BUS_WIDTH-1:0] out_tdata;
  wire out_tvalid;
  wire out_last;
  reg out_tready = 1'b1;

  ccsds123_top
    #(
      .PIPELINES(PIPELINES),
      .ISUNSIGNED(ISUNSIGNED),
      .D(D),
      .NX(NX),
      .NY(NY),
      .NZ(NZ),
      .P(P),
      .R(R),
      .OMEGA(OMEGA),
      .TINC_LOG(TINC_LOG),
      .V_MIN(V_MIN),
      .V_MAX(V_MAX),
      .UMAX(UMAX),
      .COUNTER_SIZE(COUNTER_SIZE),
      .INITIAL_COUNT(INITIAL_COUNT),
      .KZ_PRIME(KZ_PRIME),
      .COL_ORIENTED(COL_ORIENTED),
      .REDUCED(REDUCED),
      .BUS_WIDTH(BUS_WIDTH)
    )
    i_top (
      .clk(clk),
      .aresetn(aresetn),
      .s_axis_tdata(in_tdata),
      .s_axis_tvalid(in_tvalid),
      .s_axis_tready(in_tready),
      .m_axis_tdata(out_tdata),
      .m_axis_tvalid(out_tvalid),
      .m_axis_tlast(out_last),
      .m_axis_tready(out_tready)
    );

  always #(PERIOD/2) clk = ~clk;

  reg [1023:0] in_filename;
  reg [1023:0] out_dir;
  reg [1023:0] out_filename;

  integer in_file;
  integer out_file;
  integer next_sample_index;
  integer input_bytes;
  integer output_bytes;
  integer total_cycles;
  integer stalled_cycles;

  reg [D-1:0] sample_mem [0:TOTAL_SAMPLES-1];

  task automatic load_word(output reg [PIPELINES*D-1:0] word, output integer samples_loaded);
    integer lane;
    integer pixel_index;
    integer band_index;
    integer band_stride;
    integer x_coord;
    integer y_coord;
    integer bsq_index;
    reg [D-1:0] sample_word;
  begin
    word = {PIPELINES*D{1'b0}};
    samples_loaded = 0;
    band_stride = NX * NY;
    for (lane = 0; lane < PIPELINES; lane = lane + 1) begin
      sample_word = {D{1'b0}};
      if (next_sample_index < TOTAL_SAMPLES) begin
        pixel_index = next_sample_index / NZ;
        band_index = next_sample_index % NZ;
        x_coord = pixel_index % NX;
        y_coord = pixel_index / NX;
        bsq_index = band_index * band_stride + y_coord * NX + x_coord;
        sample_word = sample_mem[bsq_index];
        next_sample_index = next_sample_index + 1;
        samples_loaded = samples_loaded + 1;
      end
      word[lane*D +: D] = sample_word;
    end
  end
  endtask

  initial begin : drive_inputs
    integer samples_loaded;
    reg [PIPELINES*D-1:0] next_word;

    input_bytes = 0;
    next_sample_index = 0;
    total_cycles = 0;
    stalled_cycles = 0;

    repeat (4) @(posedge clk);
    aresetn <= 1'b1;

    if (!$value$plusargs("IN_FILENAME=%s", in_filename)) begin
      in_filename = "test.bin";
    end

    in_file = $fopen(in_filename, "rb");
    if (in_file == 0) begin
      $display("ERROR: Failed to open input file %s", in_filename);
      $finish;
    end

    begin : load_input_file
      integer sample_idx;
      integer byte_index;
      integer value;
      reg [7:0] byte_value;
      reg [D-1:0] sample_word;
      for (sample_idx = 0; sample_idx < TOTAL_SAMPLES; sample_idx = sample_idx + 1) begin
        sample_word = {D{1'b0}};
        for (byte_index = 0; byte_index < SAMPLE_BYTES; byte_index = byte_index + 1) begin
          value = $fgetc(in_file);
          if (value == -1) begin
            $display("ERROR: Unexpected end of input at sample %0d, byte %0d", sample_idx, byte_index);
            value = 0;
          end else begin
            input_bytes = input_bytes + 1;
          end
          byte_value = value[7:0];
          sample_word[byte_index*8 +: 8] = byte_value;
        end
        sample_mem[sample_idx] = sample_word;
      end
    end
    $fclose(in_file);

    next_sample_index = 0;

    load_word(next_word, samples_loaded);
    in_tdata <= next_word;
    in_tvalid <= (samples_loaded > 0);

    while (in_tvalid || next_sample_index < TOTAL_SAMPLES) begin
      @(posedge clk);
      total_cycles = total_cycles + 1;
      if (in_tvalid && !in_tready) begin
        stalled_cycles = stalled_cycles + 1;
      end
      if (in_tvalid && in_tready) begin
        if (next_sample_index < TOTAL_SAMPLES) begin
          load_word(next_word, samples_loaded);
          in_tdata <= next_word;
          in_tvalid <= (samples_loaded > 0);
        end else begin
          in_tvalid <= 1'b0;
        end
      end else if (!in_tvalid && next_sample_index < TOTAL_SAMPLES) begin
        load_word(next_word, samples_loaded);
        in_tdata <= next_word;
        in_tvalid <= (samples_loaded > 0);
      end
    end

    @(posedge clk);
    in_tvalid <= 1'b0;
  end

  initial begin : capture_outputs
    integer byte_idx;
    real compression_ratio;

    output_bytes = 0;

    if (!$value$plusargs("OUT_DIR=%s", out_dir)) begin
      out_dir = ".";
    end
    $sformat(out_filename, "%0s/out.bin", out_dir);

    out_file = $fopen(out_filename, "wb");
    if (out_file == 0) begin
      $display("ERROR: Failed to open output file %s", out_filename);
      $finish;
    end

    wait (aresetn == 1'b1);

    forever begin
      @(posedge clk);
      if (out_tvalid && out_tready) begin
        for (byte_idx = 0; byte_idx < BUS_WIDTH/8; byte_idx = byte_idx + 1) begin
          $fwrite(out_file, "%c", out_tdata[byte_idx*8 +: 8]);
          output_bytes = output_bytes + 1;
        end
        if (out_last) begin
          $fclose(out_file);
          $display("HDL payload bytes: %0d", output_bytes);
          if (output_bytes > 0) begin
            compression_ratio = input_bytes * 1.0 / output_bytes;
            $display("Compression ratio (input/output): %f", compression_ratio);
          end else begin
            $display("Compression ratio unavailable: zero payload");
          end
          if (total_cycles > 0) begin
            $display("Input stream stalled %0d of %0d cycles (%f%%)", stalled_cycles, total_cycles,
                     100.0 * stalled_cycles / total_cycles);
          end
          $finish;
        end
      end
    end
  end

  always @(posedge clk) begin
    out_tready <= 1'b1;
  end

endmodule
