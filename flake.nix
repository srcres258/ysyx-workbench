{
  description = "Nix configuration for my YSYX project.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self, nixpkgs, flake-utils
  }: flake-utils.lib.eachDefaultSystem (system: let
    pkgs = nixpkgs.legacyPackages.${system};
    buildDeps = with pkgs; [
      gcc
      gnumake
      pkg-config

      verilator
      gtkwave
      circt
      iverilog

      # needed for building NEMU kconfig tools (mconf)
      ncurses
      flex
      bison
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
    ];
    # Safe for LD_LIBRARY_PATH — excludes libz which conflicts with binutils' own zlib
    runtimeLibDeps = with pkgs; [
      SDL2
      SDL2_image
      SDL2_ttf
      sdl3
      sdl3-ttf
      sdl3-image
      libelf
      capstone
      readline
    ];
  in {
    devShells.default = pkgs.mkShell {
      nativeBuildInputs = buildDeps;
      buildInputs = runtimeDeps;

      hardeningDisable = [ "all" ];

      packages = with pkgs; [
        verilator
      ];

      shellHook = ''
        export PKG_CONFIG_PATH="${pkgs.lib.makeSearchPath "lib/pkgconfig" runtimeDeps}"
        export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath runtimeLibDeps}:$LD_LIBRARY_PATH"

        export NVBOARD_HOME="$PWD/nvboard"
        export AM_HOME="$PWD/abstract-machine"
        export NPC_HOME="$PWD/npc"
        export VERILATOR_HOME="${pkgs.verilator}/share/verilator"
        export NEMU_HOME="$PWD/nemu"
        export YSYX_HOME="$PWD"
      '';
    };
  });
}

