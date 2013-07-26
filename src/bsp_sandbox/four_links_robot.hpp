#pragma once

#include "bsp/bsp.hpp"
#include "bsp/openrave_utils.hpp"
#include "trajopt/kinematic_terms.hpp"
#include "trajopt/problem_description.hpp"
#include "geometry_2d.hpp"
#include <QtCore>
#include <QImage>
#include <qevent.h>
#include <openrave-core.h>
#include <openrave/openrave.h>
#include <array>

using namespace BSP;
using namespace Geometry2D;

namespace FourLinksRobotBSP {

  // This will generate a bunch of types like StateT, ControlT, etc.
  BSP_TYPEDEFS(
      4, // state_dim
      4, // state_noise_dim
      4, // control_dim
      3, // observe_dim
      3, // observe_noise_dim
      10, // sigma_dof
      14 // belief_dim
  );

  typedef Matrix<double, 2, 4> TransT;

  // state: { {robot_dofs} }
  // control: { {d_robot_dofs} }
  // observation: { (whatever) }

  class FourLinksRobotBSPProblemHelper;
  typedef boost::shared_ptr<FourLinksRobotBSPProblemHelper> FourLinksRobotBSPProblemHelperPtr;

  class FourLinksRobotStateFunc : public StateFunc<StateT, ControlT, StateNoiseT> {
  public:
    typedef boost::shared_ptr<FourLinksRobotStateFunc> Ptr;
    FourLinksRobotBSPProblemHelperPtr four_links_robot_helper;

    FourLinksRobotStateFunc();
    FourLinksRobotStateFunc(BSPProblemHelperBasePtr helper);
    StateT operator()(const StateT& x, const ControlT& u, const StateNoiseT& m) const ;
  };

  class FourLinksRobotObserveFunc : public ObserveFunc<StateT, ObserveT, ObserveNoiseT> {
  public:
    typedef boost::shared_ptr<FourLinksRobotObserveFunc> Ptr;
    FourLinksRobotBSPProblemHelperPtr four_links_robot_helper;
    FourLinksRobotObserveFunc();
    FourLinksRobotObserveFunc(BSPProblemHelperBasePtr helper);
    ObserveT operator()(const StateT& x, const ObserveNoiseT& n) const;
  };

  class FourLinksRobotBeliefFunc : public EkfBeliefFunc<FourLinksRobotStateFunc, FourLinksRobotObserveFunc, BeliefT> {
  public:
    typedef boost::shared_ptr<FourLinksRobotBeliefFunc> Ptr;
    FourLinksRobotBSPProblemHelperPtr four_links_robot_helper;
    FourLinksRobotBeliefFunc();
    FourLinksRobotBeliefFunc(BSPProblemHelperBasePtr helper, StateFuncPtr f, ObserveFuncPtr h);
  };

  class FourLinksRobotBSPProblemHelper : public BSPProblemHelper<FourLinksRobotBeliefFunc> {
  public:
    typedef typename BeliefConstraint<FourLinksRobotBeliefFunc>::Ptr BeliefConstraintPtr;
    void add_goal_constraint(OptProb& prob);
    void add_collision_term(OptProb& prob);
    void configure_problem(OptProb& prob);
    FourLinksRobotBSPProblemHelper();

    Vector4d link_lengths;
    RobotBasePtr robot;
    RobotAndDOFPtr rad;
    KinBody::LinkPtr link;
    Matrix4d goal_trans;
  };

  class FourLinksRobotOptimizerTask : public BSPOptimizerTask,
                                    public OpenRAVEPlotterMixin<FourLinksRobotBSPProblemHelper> {
  public:
    FourLinksRobotOptimizerTask(QObject* parent=nullptr);
    FourLinksRobotOptimizerTask(int argc, char **argv, QObject* parent=nullptr);
    void run();
  };

  class FourLinksRobotGoalError : public VectorOfVector {
  public:
    FourLinksRobotBSPProblemHelperPtr helper;
    FourLinksRobotGoalError(FourLinksRobotBSPProblemHelperPtr helper);
    VectorXd operator()(const VectorXd& a) const;
  };

  class FourLinksRobotBSPPlanner : public BSPPlanner<FourLinksRobotBSPProblemHelper> {
  public:
    void initialize();
    FourLinksRobotBSPPlanner();
    void initialize_optimizer_parameters(BSPTrustRegionSQP& opt, bool is_first_time=true);
    RobotBasePtr robot;
    RobotAndDOFPtr rad;
    Vector2d goal_pos;
    KinBody::LinkPtr link;
  };

  typedef boost::shared_ptr<FourLinksRobotBSPPlanner> FourLinksRobotBSPPlannerPtr;


}
