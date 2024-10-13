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
            clang-tools_18
            cmake
            gdb
            ninja
            clang_18
            libllvm
            cmakeCurses

            #perf
            tracy

            #for testing
            lit
            hyperfine
            # python3
            (pkgs.python3.withPackages (python-pkgs: [
                python-pkgs.pandas
                python-pkgs.matplotlib
              ]))
            # valgrind
            # kdePackages.kcachegrind

            #for codegen for now
            nasm
            asmjit
            elfio

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
