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
        export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath runtimeDeps}:$LD_LIBRARY_PATH"

        #export CPATH="${pkgs.verilator}/share/verilator/include:$CPATH"
        #export LIBRARY_PATH="${pkgs.verilator}/lib:$LIBRARY_PATH"

        export NVBOARD_HOME="${builtins.getEnv "PWD"}/nvboard"
        export AM_HOME="${builtins.getEnv "PWD"}/abstract-machine"
        export NPC_HOME="${builtins.getEnv "PWD"}/npc"
        export VERILATOR_HOME="${pkgs.verilator}/share/verilator"
        export NEMU_HOME="${builtins.getEnv "PWD"}/nemu"
        export YSYX_HOME="${builtins.getEnv "PWD"}"
      '';
    };
  });
}

