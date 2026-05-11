pkgs: attrs: with pkgs; rec {
  version = "1.6.48";

  src = fetchFromGitHub {
    owner = "pnggroup";
    repo = "libpng";
    rev = "v${version}";
    hash = "sha256-bJ1qNNca0tG1UzJCPk+yCxZNL3EvTdndX0UqRcjP4tw=";
  };

  # nixpkgs applies apng patch from sourceforge.net, which changes for every libpng version.
  # We apply a sligthly modified version of this patch via patches/apng.patch
  patches = [];
  postPatch = "";

  configureFlags = [
    "--build=x86_64-pc-linux-gnu"
  ];
}
