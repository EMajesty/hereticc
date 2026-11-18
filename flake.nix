{
  description = "NEUROGENIC hardware-independent music workstation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    miniaudio = { url = "github:mackron/miniaudio/0.11.21"; flake = false; };
    libremidi = { url = "github:celtera/libremidi/v5.4.2"; flake = false; };
    sol2 = { url = "github:ThePhD/sol2/v3.5.0"; flake = false; };
    lua = { url = "https://www.lua.org/ftp/lua-5.4.8.tar.gz"; flake = false; };
  };

  outputs = inputs@{ self, nixpkgs, miniaudio, libremidi, sol2, lua, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      neurogenic = pkgs.stdenv.mkDerivation {
        pname = "neurogenic";
        version = "0.1.0";
        src = pkgs.lib.cleanSourceWith {
          src = self;
          filter = path: type:
            let name = baseNameOf path;
            in !(builtins.elem name [ "result" "results" ]
              || (type == "directory" && name == "build"));
        };
        nativeBuildInputs = with pkgs; [ cmake ninja pkg-config ];
        buildInputs = with pkgs; [ alsa-lib ];
        cmakeFlags = [
          "-DNEUROGENIC_FETCH_DEPENDENCIES=ON"
          "-DNEUROGENIC_BUILD_TESTS=ON"
          "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          "-DFETCHCONTENT_SOURCE_DIR_MINIAUDIO=${miniaudio}"
          "-DFETCHCONTENT_SOURCE_DIR_LIBREMIDI=${libremidi}"
          "-DFETCHCONTENT_SOURCE_DIR_SOL2=${sol2}"
          "-DFETCHCONTENT_SOURCE_DIR_LUA=${lua}"
        ];
        doCheck = true;
        checkPhase = "ctest --output-on-failure";
      };
    in {
      packages.${system}.default = neurogenic;
      checks.${system} = {
        build-and-test = neurogenic;
      };
      apps.${system}.default = {
        type = "app";
        program = "${neurogenic}/bin/neurogenic";
        meta.description = "Run NEUROGENIC";
      };
      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ neurogenic ];
        packages = with pkgs; [ gcc gdb cmake ninja pkg-config git cacert direnv ];
        shellHook = ''
          echo "NEUROGENIC: cmake --preset dev && cmake --build --preset dev && ctest --preset dev"
        '';
      };
    };
}
