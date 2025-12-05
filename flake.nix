{
  description = "Nix configuration for my YSYX project.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    my-system.url = "github:srcres258/my-nixos-config";
  };

  outputs = {
    self, nixpkgs, flake-utils, my-system
  }: flake-utils.lib.eachDefaultSystem (system: let
    pkgs = nixpkgs.legacyPackages.${system};
    buildDeps = with pkgs; [
      gcc
      gnumake
      pkg-config

      verilator
    ];
    runtimeDeps = with pkgs; [
      SDL2
    ];
  in {
    devShells.default = my-system.devShells.${system}.srcres-full.overrideAttrs (old: {
      nativeBuildInputs = (old.nativeBuildInputs or []) ++ buildDeps;
      buildInputs = (old.buildInputs or []) ++ runtimeDeps;

      hardeningDisable = [ "all" ];

      shellHook = ''
        export PKG_CONFIG_PATH="${pkgs.lib.makeSearchPath "lib/pkgconfig" runtimeDeps}"
        export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath runtimeDeps}:$LD_LIBRARY_PATH"

        export NVBOARD_HOME="${builtins.getEnv "PWD"}/nvboard"
        export AM_HOME="${builtins.getEnv "PWD"}/abstract-machine"
        export NPC_HOME="${builtins.getEnv "PWD"}/npc"
      '' + (old.shellHook or "");
    });
  });
}

