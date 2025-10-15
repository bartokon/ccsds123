#include "ccsds123.hpp"

#include <cassert>
#include <iostream>

using namespace ccsds123;

int main() {
  Ccsds123Params params;
  params.D = 12;
  params.NX = 4;
  params.NY = 4;
  params.NZ = 3;
  params.P = 1;
  params.full_mode = true;
  params.Omega = 8;
  params.Rbits = 16;
  params.az = {0, 0, 0};
  params.rz = {0, 0, 0};
  params.Theta = 1;
  params.phi = {0, 0, 0};
  params.psi = {0, 0, 0};

  Ccsds123 codec(params);

  std::cout << "Parameter validation passed\n";
  return 0;
}
