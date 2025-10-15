from pathlib import Path
import json
import sys

from ccsds_lib import write_sim_params, write_vhdl_params

def main():
    if len(sys.argv) < 2:
        print("usage: gen_impl_params.py <config>")
        return -1

    config_path = Path(sys.argv[1]).expanduser().resolve()

    with config_path.open('r', encoding='utf-8') as config_file:
        config = json.load(config_file)

    parameters = config['parameters']
    image = config['images'][0]
    dimensions = (image["NX"], image["NY"], image["NZ"])
    signed = str(image.get("signed", "false")).lower() == "true"

    repo_root = Path(__file__).resolve().parents[1]
    tb_dir = repo_root / "hdl" / "tb"
    tb_dir.mkdir(parents=True, exist_ok=True)

    # Write verilog parameter file to be included in test bench
    write_sim_params(dimensions, parameters, signed, str(tb_dir / "impl_params.v"))
    write_vhdl_params(dimensions, parameters, signed, str(tb_dir / "synth_params.vhd"))

main()
