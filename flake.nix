{
  description = "Foptim";

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
            llvmPackages_20.libllvm
            argparse
            fmt
            unordered_dense
            #for codegen
            elfio

            #build tools
            cmake
            ninja
            llvmPackages_20.clang
            gtest

            #for testing stuff not real dependencies
            cmakeCurses
            (pkgs.python312.withPackages (python-pkgs: [
                python-pkgs.pandas
                python-pkgs.matplotlib
                python-pkgs.z3-solver
                python-pkgs.streamlit
                python-pkgs.pandas
                python-pkgs.plotly
              ]))
            graphviz
            cvise
            tracy
            nnd
            gdb
            lit
            hyperfine
            (cutter.withPlugins (ps: with ps; [ rz-ghidra ]))
            valgrind
            kdePackages.kcachegrind

            perf
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
