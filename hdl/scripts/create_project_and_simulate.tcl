
# Vivado batch script that builds the CCSDS 123 HDL project and optionally runs simulation.
# Arguments:
#   <build_dir> <project_name> <part> <board_part> <sim_top> [project-only|simulate]

proc usage {} {
    puts "Usage: vivado -mode batch -source create_project_and_simulate.tcl -- <build_dir> <project_name> <part> <board_part> <sim_top> [project-only|simulate]"
    exit 1
}

if {${argc} < 5} {
    usage
}

set build_dir    [file normalize [lindex $argv 0]]
set project_name [lindex $argv 1]
set part         [lindex $argv 2]
set board_part   [lindex $argv 3]
set sim_top      [lindex $argv 4]
set action       "simulate"
if {${argc} > 5} {
    set action [lindex $argv 5]
}

if {![string match {project-only} $action] && ![string match {simulate} $action]} {
    puts "Unknown action '$action'."
    usage
}

proc gather_files {dir pattern} {
    if {![file isdirectory $dir]} {
        return {}
    }
    return [lsort [glob -nocomplain -directory $dir $pattern]]
}

set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize [file join $script_dir .. ..]]
set tools_dir  [file join $repo_root tools]
set hdl_root   [file normalize [file join $script_dir ..]]
set src_dir    [file join $hdl_root src]
set tb_dir     [file join $hdl_root tb]

set config_file [file join $tools_dir conf.json]
if {[info exists ::env(HDL_CONFIG)]} {
    set config_file [file normalize $::env(HDL_CONFIG)]
}

if {![file exists $config_file]} {
    puts "Configuration file '$config_file' not found."
    exit 1
}

set sim_param_file [file join $tb_dir impl_params.v]
set synth_param_file [file join $tb_dir synth_params.vhd]

foreach param_file [list $sim_param_file $synth_param_file] {
    if {![file exists $param_file]} {
        puts "Required parameter file '$param_file' not found."
        puts "Run the external parameter generation helper (make hdl-params) before invoking this script."
        exit 1
    }
}

file mkdir $build_dir
set project_dir [file join $build_dir $project_name]

create_project $project_name $project_dir -force -part $part
if {$board_part ne ""} {
    catch {set_property board_part $board_part [current_project]}
}
set_property target_language VHDL [current_project]

set design_set [get_filesets sources_1]
set sim_set    [get_filesets sim_1]

set design_vhdl   [gather_files $src_dir *.vhd]
set design_verilog [gather_files $src_dir *.v]
set tb_vhdl       [gather_files $tb_dir *.vhd]
set tb_verilog    [gather_files $tb_dir *.v]

if {[llength $design_vhdl] > 0} {
    add_files -fileset $design_set $design_vhdl
    add_files -fileset $sim_set $design_vhdl
}
if {[llength $design_verilog] > 0} {
    add_files -fileset $design_set $design_verilog
    add_files -fileset $sim_set $design_verilog
}
if {[llength $tb_vhdl] > 0} {
    add_files -fileset $sim_set $tb_vhdl
}
if {[llength $tb_verilog] > 0} {
    add_files -fileset $sim_set $tb_verilog
}

update_compile_order -fileset $design_set
update_compile_order -fileset $sim_set

if {$action eq "project-only"} {
    puts "Vivado project created at $project_dir"
    close_project
    exit 0
}

set sim_options {}
if {[info exists ::env(HDL_INPUT_FILE)]} {
    set in_file [file normalize $::env(HDL_INPUT_FILE)]
    lappend sim_options "-testplusarg {IN_FILENAME=$in_file}"
}
if {[info exists ::env(HDL_OUTPUT_DIR)]} {
    set out_dir [file normalize $::env(HDL_OUTPUT_DIR)]
    lappend sim_options "-testplusarg {OUT_DIR=$out_dir}"
}
if {[llength $sim_options] > 0} {
    set_property -name {xsim.simulate.xsim.more_options} -value [join $sim_options " "] -objects [get_filesets sim_1]
}

set_property top $sim_top $sim_set
launch_simulation -simset $sim_set -mode behavioral
run all
quit
