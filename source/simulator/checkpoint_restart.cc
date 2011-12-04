/* $Id$ */
/* Author: Martin Kronbichler, Uppsala University,
           Wolfgang Bangerth, Texas A&M University,
     Timo Heister, University of Goettingen, 2008-2011 */
/*                                                                */
/*    Copyright (C) 2008, 2009, 2010, 2011 by the deal.II authors */
/*                                                                */
/*    This file is subject to QPL and may not be  distributed     */
/*    without copyright and license information. Please refer     */
/*    to the file deal.II/doc/license.html for the  text  and     */
/*    further information on this license.                        */

#include <aspect/simulator.h>

#include <deal.II/grid/grid_tools.h>
#include <deal.II/distributed/solution_transfer.h>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>



namespace aspect
{
  namespace
  {
    /**
     * Move/rename a file from the given old to the given new name.
     */
    void move_file (const std::string &old_name,
                    const std::string &new_name)
    {
      const int error = system (("mv " + old_name + " " + new_name).c_str());

      if (error != 0)
        {
          // allocate a static string that
          // holds the message for long enough
          // so that it can be printed
          static const std::string
          error_message (std::string ("Can't move files: ")
                         +
                         old_name + " -> " + new_name);
          AssertThrow (error == 0, ExcMessage(error_message.c_str()));
        }
    }
  }


  template <int dim>
  void Simulator<dim>::create_snapshot()
  {
    unsigned int myid = Utilities::System::get_this_mpi_process (MPI_COMM_WORLD);

    if (myid == 0)
      {
        // keep the last snapshot in case this one fails to save
        move_file (parameters.output_directory + "mesh",
                   parameters.output_directory + "mesh.old");
        move_file (parameters.output_directory + "mesh.info",
                   parameters.output_directory + "mesh.info.old");
        move_file (parameters.output_directory + "resume.txt",
                   parameters.output_directory + "resume.txt.old");
      }

    //save Triangulation and Solution vectors:
    {
      std::vector<const TrilinosWrappers::MPI::Vector *> x_temperature (3);
      x_temperature[0] = &temperature_solution;
      x_temperature[1] = &old_temperature_solution;
      x_temperature[2] = &old_old_temperature_solution;
      std::vector<const TrilinosWrappers::MPI::BlockVector *> x_stokes (2);
      x_stokes[0] = &stokes_solution;
      x_stokes[1] = &old_stokes_solution;

      parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
      temperature_trans (temperature_dof_handler);
      parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::BlockVector>
      stokes_trans (stokes_dof_handler);

      temperature_trans.prepare_serialization (x_temperature);
      stokes_trans.prepare_serialization (x_stokes);

      triangulation.save ((parameters.output_directory + "mesh").c_str());
    }

    //save general information
    if (myid == 0)
      {
        std::ofstream ofs ((parameters.output_directory + "resume.txt").c_str());
        boost::archive::text_oarchive oa (ofs);
        oa << (*this);
      }
    pcout << "*** Snapshot created!" << std::endl;
  }



  template <int dim>
  void Simulator<dim>::resume_from_snapshot()
  {
    triangulation.load ((parameters.output_directory + "mesh").c_str());
    global_volume = GridTools::volume (triangulation, mapping);
    setup_dofs();

    TrilinosWrappers::MPI::Vector
    distributed_temp1 (temperature_rhs);
    TrilinosWrappers::MPI::Vector
    distributed_temp2 (temperature_rhs);
    TrilinosWrappers::MPI::Vector
    distributed_temp3 (temperature_rhs);

    std::vector<TrilinosWrappers::MPI::Vector *> x_temperature (3);
    x_temperature[0] = & (distributed_temp1);
    x_temperature[1] = & (distributed_temp2);
    x_temperature[2] = & (distributed_temp3);

    TrilinosWrappers::MPI::BlockVector
    distributed_stokes (stokes_rhs);
    TrilinosWrappers::MPI::BlockVector
    old_distributed_stokes (stokes_rhs);
    std::vector<TrilinosWrappers::MPI::BlockVector *> x_stokes (2);
    x_stokes[0] = & (distributed_stokes);
    x_stokes[1] = & (old_distributed_stokes);

    parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    temperature_trans (temperature_dof_handler);
    parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::BlockVector>
    stokes_trans (stokes_dof_handler);

    temperature_trans.deserialize (x_temperature);
    stokes_trans.deserialize (x_stokes);


    temperature_solution = distributed_temp1;
    old_temperature_solution = distributed_temp2;
    old_old_temperature_solution = distributed_temp3;

    stokes_solution = distributed_stokes;
    old_stokes_solution = old_distributed_stokes;

    std::ifstream ifs ((parameters.output_directory + "resume.txt").c_str());
    boost::archive::text_iarchive ia (ifs);
    ia >> (*this);

    // re-initialize the postprocessors with the current object
    postprocess_manager.initialize (*this);

    pcout << "*** resuming from Snapshot!" << std::endl;
  }

}

//why do we need this?!
BOOST_CLASS_TRACKING (aspect::Simulator<2>, boost::serialization::track_never)
BOOST_CLASS_TRACKING (aspect::Simulator<3>, boost::serialization::track_never)


namespace aspect
{

  template <int dim>
  template<class Archive>
  void Simulator<dim>::serialize (Archive &ar, const unsigned int)
  {
    ar &time;
    ar &time_step;
    ar &old_time_step;
    ar &timestep_number;

    ar &postprocess_manager &statistics;

  }
}


// explicit instantiation of the functions we implement in this file
namespace aspect
{
  template
  class Simulator<deal_II_dimension>;
}
