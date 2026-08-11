{
  description = "Nix configuration for my YSYX project.";

  nixConfig = {
    extra-substituters = [
      "https://nur-packages-srcres258.cachix.org?priority=10"
    ];
    extra-trusted-public-keys = [
      "nur-packages-srcres258.cachix.org-1:3Nn/bNoV5HnI1RoN5uOtkHrPq078veIhjJZmF0FUGP0="
    ];
  };

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    nurPackages = {
      url = "github:srcres258/nur-packages";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = {
    self, nixpkgs, flake-utils, nurPackages
  }: flake-utils.lib.eachDefaultSystem (system: let
    pkgs = nixpkgs.legacyPackages.${system};
    hasIeda = system == "x86_64-linux";
    ieda = nurPackages.packages.${system}.ieda;
    openjdk21 = pkgs.openjdk21;
    firtool = pkgs.circt;
    pythonEnv = pkgs.python313.withPackages (pythonPackages: with pythonPackages; [ matplotlib numpy pytest ]);
    bashEnv = pkgs.writeText "ysyx-bash-env" ''
      export PATH="${pythonEnv}/bin:$PATH"
    '';
    buildDeps = with pkgs; [
      ccache
      gcc
      gnumake
      pkg-config

      openjdk21
      mill
      pythonEnv

      verilator
      gtkwave
      firtool
      iverilog
      yosys

      # needed for building NEMU kconfig tools (mconf)
      ncurses
      flex
      bison

      pkgsCross.riscv64.stdenv.cc # Linux GNU
      pkgsCross.riscv64-embedded.stdenv.cc # bare-metal ELF
      pkgsCross.riscv32-embedded.stdenv.cc
    ];
    runtimeDeps = with pkgs; [
      SDL2
      SDL2_image
      SDL2_ttf
      sdl3
      sdl3-ttf
      sdl3-image

      libelf
      libz
      capstone
      readline

      gtest
      
      bzip2
    ];
    # Safe for LD_LIBRARY_PATH — excludes libz which conflicts with binutils' own zlib
    runtimeLibDeps = with pkgs; [
      SDL2
      SDL2_image
      SDL2_ttf
      sdl3
      sdl3-ttf
      sdl3-image
      libunwind
      libelf
      capstone
      readline
      stdenv.cc.cc.lib
    ];
  in {
    devShells.default = pkgs.mkShell {
      nativeBuildInputs = buildDeps;
      buildInputs = runtimeDeps;

      hardeningDisable = [ "all" ];

      packages = [
        pkgs.verilator
      ] ++ pkgs.lib.optionals hasIeda [
        ieda
      ];

      shellHook = ''
        export BASH_ENV="${bashEnv}"

        export PKG_CONFIG_PATH="${pkgs.lib.makeSearchPath "lib/pkgconfig" runtimeDeps}:$PKG_CONFIG_PATH"
        export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath runtimeLibDeps}:$LD_LIBRARY_PATH"

        export NVBOARD_HOME="$PWD/nvboard"
        export AM_HOME="$PWD/abstract-machine"
        export NPC_HOME="$PWD/npc"
        export YOSYS_STA_HOME="$PWD/yosys-sta"
        export VERILATOR_HOME="${pkgs.verilator}/share/verilator"
        export NEMU_HOME="$PWD/nemu"
        export YSYX_HOME="$PWD"

        ${pkgs.lib.optionalString hasIeda ''
        export IEDA_BIN="${ieda}/bin/iEDA"
        ''}
      '';
    };
  });
}
