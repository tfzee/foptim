{
  description = "FoffEng";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem
      (system:
        let
          overlays = [];
          pkgs = import nixpkgs {
            inherit system overlays;
          };
          nativeBuildInputs = with pkgs; [
            # IMPORTANT CLANG TOOLS AT START
            llvmPackages_20.clang-tools

            #depend
            libllvm
            argparse
            fmt
            #for codegen
            elfio

            #build tools
            cmakeCurses
            cmake
            ninja
            llvmPackages_20.clang

            #for testing stuff
            graphviz
            cvise
            gtest
            tracy
            gdb
            lit
            hyperfine
            (pkgs.python312.withPackages (python-pkgs: [
                python-pkgs.pandas
                python-pkgs.matplotlib
                python-pkgs.z3-solver
              ]))
            (cutter.withPlugins (ps: with ps; [ rz-ghidra ]))
            valgrind
            kdePackages.kcachegrind

            linuxKernel.packages.linux_6_6.perf
            hotspot
          ];

          buildInputs = [ ];
        in
        with pkgs;
        {
          devShells.default = mkShell {
            inherit buildInputs nativeBuildInputs;
            shellHook = ''
                export TRACY_NO_EXIT=1
                export QT_QPA_PLATFORM=xcb
                export NIX_ENFORCE_NO_NATIVE=0
            '';
          };
        }
      );
}
