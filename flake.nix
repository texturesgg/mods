{
  description = "The official tgg-melee mods: the tools that build them";

  # tgg-melee's own nixpkgs (its flake.lock), so mods compile with the GCC
  # the game is built with
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/c59305bab2065cfecc4944690d9eedbb56f3a9fa";

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      mingw = pkgs.pkgsCross.mingwW64;
      tools = [
        pkgs.cmake
        pkgs.ninja
        pkgs.binutils
        pkgs.jq
        pkgs.curl
      ];
    in
    {
      devShells.${system} = {
        # Mods for Linux
        default = pkgs.mkShell { packages = tools ++ [ pkgs.gcc ]; };
        # Mods for Windows, cross-built with MinGW-w64 GCC as the game is
        windows = pkgs.mkShell {
          packages = tools ++ [ mingw.buildPackages.gcc ];
          TGG_MINGW_MCFGTHREADS = "${mingw.windows.mcfgthreads}";
        };
      };
    };
}
