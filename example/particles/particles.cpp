//========================================================================================
// (C) (or copyright) 2020. Triad National Security, LLC. All rights reserved.
//
// This program was produced under U.S. Government contract 89233218CNA000001 for Los
// Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
// for the U.S. Department of Energy/National Nuclear Security Administration. All rights
// in the program are reserved by Triad National Security, LLC, and the U.S. Department
// of Energy/National Nuclear Security Administration. The Government is granted for
// itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
// license in this material to reproduce, prepare derivative works, distribute copies to
// the public, perform publicly and display publicly, and to permit others to do so.
//========================================================================================

#include "particles.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bvals/boundary_conditions.hpp"
#include "bvals/bvals.hpp"
#include "driver/multistage.hpp"
#include "interface/params.hpp"
#include "interface/state_descriptor.hpp"
#include "mesh/mesh.hpp"
#include "parthenon_manager.hpp"
#include "reconstruct/reconstruction.hpp"
#include "refinement/refinement.hpp"

using parthenon::BlockStageNamesIntegratorTask;
using parthenon::BlockStageNamesIntegratorTaskFunc;
using parthenon::BlockTask;
using parthenon::CellVariable;
using parthenon::Integrator;
using parthenon::Metadata;
using parthenon::Params;
using parthenon::ParArrayND;
using parthenon::ParthenonManager;

// *************************************************//
// redefine some weakly linked parthenon functions *//
// *************************************************//

namespace parthenon {

Packages_t ParthenonManager::ProcessPackages(std::unique_ptr<ParameterInput> &pin) {
  Packages_t packages;
  packages["Particles"] = particles_example::Particles::Initialize(pin.get());
  return packages;
}

void MeshBlock::ProblemGenerator(ParameterInput *pin) {
  SwarmContainer &sc = real_containers.GetSwarmContainer();
  Swarm &s = sc.Get("my particles");
  //CellVariable<Real> &q = rc.Get("advected");

  // We should create some particles here
  s.AddParticle();



  /*for (int k = 0; k < ncells3; k++) {
    for (int j = 0; j < ncells2; j++) {
      for (int i = 0; i < ncells1; i++) {
        Real rsq = std::pow(pcoord->x1v(i), 2) + std::pow(pcoord->x2v(j), 2);
        q(k, j, i) = (rsq < 0.15 * 0.15 ? 1.0 : 0.0);
      }
    }
  }*/
}

} // namespace parthenon

// *************************************************//
// define the "physics" package Particles, which   *//
// includes defining various functions that control*//
// how parthenon functions and any tasks needed to *//
// implement the "physics"                         *//
// *************************************************//

namespace particles_example {
namespace Particles {

std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin) {
  auto pkg = std::make_shared<StateDescriptor>("Particles");

  int num_particles = pin->GetOrAddInteger("Particles", "num_particles", 100);
  pkg->AddParam<>("num_particles", num_particles);
  Real particle_speed = pin->GetOrAddReal("Particles", "particle_speed", 1.0);
  pkg->AddParam<>("particle_speed", particle_speed);

  std::string swarm_name = "my particles";
  Metadata swarm_metadata;
  pkg->AddSwarm(swarm_name, swarm_metadata);
  Metadata real_swarmvalue_metadata({Metadata::Real});
  pkg->AddSwarmValue("weight", swarm_name, real_swarmvalue_metadata);
  pkg->AddSwarmValue("vx", swarm_name, real_swarmvalue_metadata);
  pkg->AddSwarmValue("vy", swarm_name, real_swarmvalue_metadata);
  pkg->AddSwarmValue("vz", swarm_name, real_swarmvalue_metadata);

  pkg->EstimateTimestep = EstimateTimestep;

  return pkg;
}

AmrTag CheckRefinement(Container<Real> &rc) {
  return AmrTag::same;
}

Real EstimateTimestep(Container<Real> &rc) {
  return 0.5;
}

TaskStatus SetTimestepTask(Container<Real> &rc) {
  MeshBlock *pmb = rc.pmy_block;
  pmb->SetBlockTimestep(parthenon::Update::EstimateTimestep(rc));
  return TaskStatus::complete;
}

//}

} // namespace Particles

// *************************************************//
// define the application driver. in this case,    *//
// that just means defining the MakeTaskList       *//
// function.                                       *//
// *************************************************//
// first some helper tasks
TaskStatus UpdateContainer(MeshBlock *pmb, int stage,
                           std::vector<std::string> &stage_name, Integrator *integrator) {
  // const Real beta = stage_wghts[stage-1].beta;
  printf("UPDATE CONTAINER\n");
  const Real beta = integrator->beta[stage - 1];
  Container<Real> &base = pmb->real_containers.Get();
  Container<Real> &cin = pmb->real_containers.Get(stage_name[stage - 1]);
  Container<Real> &cout = pmb->real_containers.Get(stage_name[stage]);
  Container<Real> &dudt = pmb->real_containers.Get("dUdt");
  parthenon::Update::AverageContainers(cin, base, beta);
  parthenon::Update::UpdateContainer(cin, dudt, beta * pmb->pmy_mesh->dt, cout);
  return TaskStatus::complete;
}

TaskStatus MyContainerTask(Container<Real> container) {
  printf("MY CONTAINER TASK\n");
  return TaskStatus::complete;
}

TaskStatus UpdateSwarm(MeshBlock *pmb, int stage, std::vector<std::string> &stage_name,
                       Integrator *integrator) {
  printf("UPDATE SWARM\n");
  SwarmContainer &base = pmb->real_containers.GetSwarmContainer();

  // weight = sqrt(x^2 + y^2 + z^2)?

  return TaskStatus::complete;
}

// See the advection.hpp declaration for a description of how this function gets called.
TaskList ParticleDriver::MakeTaskList(MeshBlock *pmb, int stage) {
  printf("MAKE TASK LIST\n");
  TaskList tl;

  TaskID none(0);
  // first make other useful containers
  if (stage == 1) {
    Container<Real> &container = pmb->real_containers.Get();
    SwarmContainer &base = pmb->real_containers.GetSwarmContainer();
    pmb->real_containers.Add("my swarm container", base);
    pmb->real_containers.Add("my container", container);
  }

  SwarmContainer sc = pmb->real_containers.GetSwarmContainer("my swarm container");

  Swarm &swarm = sc.Get("my particles");

  auto update_swarm = tl.AddTask<TwoSwarmTask>(parthenon::Update::TransportSwarm, none,
                                               swarm, swarm);

  Container<Real> container = pmb->real_containers.Get("my container");

  auto update_container = tl.AddTask<ContainerTask>(MyContainerTask, none, container);

  // estimate next time step
  if (stage == integrator->nstages) {
    // Do nothing
    //AddContainerTask();
    //pmb->SetBlockTimestep(0.25);
    //printf("Setting dt to 0.5!\n");
  }
  return tl;
}

} // namespace particles_example
