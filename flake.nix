{
  description = "A shell with zlib";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs = {nixpkgs, ...}: let
    system = "x86_64-linux";
    pkgs = import nixpkgs {
      inherit system;

      config = {
        allowUnfree = true;
      };
    };
  in {
    devShells.${system}.default =
      pkgs.mkShell
      {
        name = "zlib-env-shell";

        nativeBuildInputs = with pkgs; [
          zlib
        ];
      };
  };
}
