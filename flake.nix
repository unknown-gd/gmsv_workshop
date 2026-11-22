{
  description = "gmsv_workshop — server steamworks.DownloadUGC and steamworks.FileInfo";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;

      mkGmWorkshop =
        {
          stdenv,
          cmake,
          lib,
          pnameSuffix ? "",
          metaPlatforms ? lib.platforms.linux,
        }:
        stdenv.mkDerivation {
          pname = "gmsv_workshop${pnameSuffix}";
          version = "1.0.0";

          src = lib.cleanSource ./.;

          nativeBuildInputs = [ cmake ];

          cmakeBuildType = "Release";
          cmakeFlags = [ "-DGM_WORKSHOP_TARGET=auto" ];

          doCheck = true;
          checkPhase = ''
            runHook preCheck
            ctest --output-on-failure
            runHook postCheck
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p $out/lib
            cp -v gmsv_workshop_*.dll $out/lib/
            runHook postInstall
          '';

          meta = with lib; {
            description = "Server-side steamworks.DownloadUGC and steamworks.FileInfo for Garry's Mod";
            license = licenses.mit;
            platforms = metaPlatforms;
          };
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          linux64 = pkgs.callPackage mkGmWorkshop {
            pnameSuffix = "-linux64";
            metaPlatforms = [ "x86_64-linux" ];
          };

          linux32 = pkgs.pkgsi686Linux.callPackage mkGmWorkshop {
            pnameSuffix = "-linux32";
            metaPlatforms = [ "i686-linux" ];
          };

          all = pkgs.symlinkJoin {
            name = "gmsv_workshop-all";
            paths = [
              linux64
              linux32
            ];
            meta = {
              description = "gmsv_workshop modules (linux32 + linux64)";
            };
          };
        in
        {
          inherit linux64 linux32 all;
          default = all;
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default =
            let
              queryDriver = "${pkgs.stdenv.cc}/bin/*";
              clangdForNix = pkgs.writeShellScriptBin "clangd" ''
                exec ${pkgs.clang-tools}/bin/clangd --query-driver=${queryDriver} "$@"
              '';
            in
            pkgs.mkShell {
              packages = [
                pkgs.cmake
                pkgs.ninja
                pkgs.gcc
                pkgs.pkgsi686Linux.gcc
                clangdForNix
                pkgs.clang-tools
              ];

              shellHook = ''
                if [ -f build/linux64/compile_commands.json ]; then
                  ln -sfn build/linux64/compile_commands.json compile_commands.json
                fi
              '';
            };
        }
      );
    };
}
