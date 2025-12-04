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
    ];
    runtimeDeps = with pkgs; [
      zlib
      openssl
      libxml2
      curl
    ];
  in {
    devShells.default = my-system.devShells.${system}.srcres-full.overrideAttrs (old: {
      nativeBuildInputs = buildDeps;
      buildInputs = (old.buildInputs or []) ++ runtimeDeps;
      # buildInputs = runtimeDeps;

      hardeningDisable = [ "all" ];

      packages = (old.packages or []) ++ (with pkgs; [
        # TODO
      ]);

      shellHook = (old.shellHook or "") + ''
        export PKG_CONFIG_PATH="${pkgs.lib.makeSearchPath "lib/pkgconfig" runtimeDeps}"
        export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath runtimeDeps}:$LD_LIBRARY_PATH"
      '';
    });
  });
}

