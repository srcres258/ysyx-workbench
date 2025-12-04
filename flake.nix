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
    ];
    runtimeDeps = with pkgs; [
      zlib
      openssl
      libxml2
      curl
    ];
  in {
    devShells.default = pkgs.mkShell {
      nativeBuildInputs = buildDeps;
      buildInputs = runtimeDeps;

      hardeningDisable = [ "all" ];

      shellHook = ''
        export PKG_CONFIG_PATH="${pkgs.lib.makeSearchPath "lib/pkgconfig" runtimeDeps}"
        export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath runtimeDeps}:$LD_LIBRARY_PATH"
      '';
    };
  });
}

