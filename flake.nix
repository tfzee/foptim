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
            #depend
            # IMPORTANT CLANG TOOLS AT START
            clang-tools_18
            libllvm
            argparse

            #build tools
            cmakeCurses
            cmake
            ninja
            clang_18

            
            #for codegen for now
            nasm
            asmjit
            # elfio

            #for testing stuff
            gtest
            tracy
            gdb
            lit
            hyperfine
            (pkgs.python3.withPackages (python-pkgs: [
                python-pkgs.pandas
                python-pkgs.matplotlib
              ]))
            # cutter
            valgrind
            kdePackages.kcachegrind

          ];

          buildInputs = [ ];
        in
        with pkgs;
        {
          devShells.default = mkShell {
            inherit buildInputs nativeBuildInputs;
            shellHook = ''
                # export CC=clang
                # export CXX=clang++ 
                export TRACY_NO_EXIT=1
            '';
          };
        }
      );}
