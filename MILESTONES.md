# Milestones

## Upcoming
- [x] Restore Vivado project generation by running implementation parameter synthesis outside the Vivado TCL environment to avoid restricted Python executables.
- [x] Align CPP, HDL, and AIE implementations for deterministic regression testing with shared stimuli.
- [x] Retire the legacy cocotb harness and introduce an automated HDL vs. C++ payload comparison.
- [x] Synchronise the sample-adaptive coder parameters, container metadata, and batch tooling with the HDL reference to support
      multi-frame raw imagery.
- [x] Validate and document parameter synchronization requirements between HDL and C++ configurations (October 2025: identified and resolved P parameter mismatch in video compression tests).
- [ ] Add near-lossless compression modes and hybrid coder pathways with ISSUE-1 compatibility switches.
- [ ] Prototype vectorised AIE data paths for accelerated processing.
- [ ] Expand automated documentation derived from `docs/` research notes.
- [ ] Implement P>0 (inter-band prediction) support in C++ encoder to match HDL capabilities.

## Future Improvements
- Harden build scripts against environment-specific tooling limitations (e.g., bundled Python runtime permissions).
- Capture lessons learned and architecture decisions in an extended developer guide.
- Add pre-flight parameter validation in comparison scripts to detect HDL/C++ configuration mismatches before simulation.
- Consider refactoring C++ encoder CLI to accept configuration from JSON file rather than hardcoding P=0.
- Document P parameter constraints and inter-band prediction limitations in system_overview.md.
- Create regression test suite specifically for parameter validation across all three implementations (C++, HDL, AIE).
