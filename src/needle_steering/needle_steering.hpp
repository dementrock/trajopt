#pragma once

#include "needle_steering.hpp"
#include "sco/expr_ops.hpp"
#include "sco/modeling_utils.hpp"
#include "sco/modeling.hpp"
#include "osgviewer/osgviewer.hpp"
#include "trajopt/collision_checker.hpp"
#include "trajopt/collision_terms.hpp"
#include "trajopt/common.hpp"
#include "trajopt/plot_callback.hpp"
#include "trajopt/problem_description.hpp"
#include "trajopt/rave_utils.hpp"
#include "trajopt/trajectory_costs.hpp"
#include "utils/clock.hpp"
#include "utils/config.hpp"
#include "utils/eigen_conversions.hpp"
#include "utils/stl_to_string.hpp"
#include <boost/assign.hpp>
#include <boost/foreach.hpp>
#include <ctime>
#include <openrave-core.h>
#include <openrave/openrave.h>

//#define NEEDLE_TEST
//#define USE_CURVATURE

#define SWITCH_DEFAULT default: \
                         PRINT_AND_THROW("not implemented");

using namespace trajopt;
using namespace std;
using namespace OpenRAVE;
using namespace util;
using namespace boost::assign;
using namespace Eigen;

namespace Needle {

  inline double bound_inf(double result, double bound);

  void AddVarArrays(OptProb& prob, int rows, const vector<int>& cols, const vector<double>& lbs, const vector<double>& ubs, const vector<string>& name_prefix, const vector<VarArray*>& newvars);

  void AddVarArrays(OptProb& prob, int rows, const vector<int>& cols, const vector<string>& name_prefix, const vector<VarArray*>& newvars);
    
  void AddVarArray(OptProb& prob, int rows, int cols, double lb, double ub, const string& name_prefix, VarArray& newvars);
    
  void AddVarArray(OptProb& prob, int rows, int cols, const string& name_prefix, VarArray& newvars);

  Matrix3d rotMat(const Vector3d& x);

  Vector3d rotVec(const Matrix3d& X);

  Matrix3d expA(const Vector3d& w);
    
  Matrix3d logInvA(const Vector3d& w);
    
  Matrix3d expRot(const Vector3d& x);
    
  Vector3d logRot(const Matrix3d& X);
    
  Matrix4d expUp(const VectorXd& x);

  VectorXd logDown(const Matrix4d& X);

  OpenRAVE::Transform matrixToTransform(const Matrix4d& X);

  OpenRAVE::Transform vecToTransform(const VectorXd& x);

  struct NeedleProblemHelper;
  typedef boost::shared_ptr<NeedleProblemHelper> NeedleProblemHelperPtr;

  class ConstantSpeedCost : public Cost {
  public:
    ConstantSpeedCost(const Var& var, double coeff, NeedleProblemHelperPtr helper);
    virtual double value(const vector<double>& xvec);
    virtual ConvexObjectivePtr convex(const vector<double>& xvec, Model* model);
  private:
    Var var;
    double coeff;
    AffExpr expr;
    NeedleProblemHelperPtr helper;
  };

  class VariableSpeedCost : public Cost {
  public:
    VariableSpeedCost(const VarVector& vars, double deviation, double coeff, NeedleProblemHelperPtr helper);
    virtual double value(const vector<double>& xvec);
    virtual ConvexObjectivePtr convex(const vector<double>& xvec, Model* model);
  private:
    VarVector vars;
    double coeff;
    double deviation;
    QuadExpr expr;
    NeedleProblemHelperPtr helper;
  };

  class RotationCost : public Cost {
  public:
    RotationCost(const VarVector& vars, double coeff, NeedleProblemHelperPtr helper);
    virtual double value(const vector<double>& xvec);
    virtual ConvexObjectivePtr convex(const vector<double>& xvec, Model* model);
  private:
    VarVector vars;
    double coeff;
    QuadExpr expr;
    NeedleProblemHelperPtr helper;
  };

  struct LocalConfiguration : public Configuration {
    KinBodyPtr body;
    Matrix4d pose;
    LocalConfiguration(KinBodyPtr body, const Matrix4d& pose);
    LocalConfiguration(KinBodyPtr body);
    virtual void SetDOFValues(const DblVec& dofs);
    virtual void GetDOFLimits(DblVec& lower, DblVec& upper) const;
    virtual DblVec GetDOFValues();
    virtual int GetDOF() const;
    virtual OpenRAVE::EnvironmentBasePtr GetEnv();
    virtual DblMatrix PositionJacobian(int link_ind, const OpenRAVE::Vector& pt) const;
    virtual DblMatrix RotationJacobian(int link_ind) const;
    virtual bool DoesAffect(const KinBody::Link& link);
    virtual std::vector<KinBody::LinkPtr> GetAffectedLinks();
    virtual void GetAffectedLinks(std::vector<KinBody::LinkPtr>& links,
        bool only_with_geom, vector<int>& link_inds);
    virtual DblVec RandomDOFValues();
    virtual vector<OpenRAVE::KinBodyPtr> GetBodies();
  };

  typedef boost::shared_ptr<LocalConfiguration> LocalConfigurationPtr;

  struct PositionError : public VectorOfVector {
    LocalConfigurationPtr cfg;
    KinBodyPtr body;
    VectorXd target_pos;
    NeedleProblemHelperPtr helper;
    PositionError(LocalConfigurationPtr cfg, const VectorXd& target_pos, NeedleProblemHelperPtr helper);
    VectorXd operator()(const VectorXd& a) const;
  };

  struct ControlError : public VectorOfVector {
    LocalConfigurationPtr cfg0, cfg1;
    KinBodyPtr body;
    NeedleProblemHelperPtr helper;
    ControlError(LocalConfigurationPtr cfg0, LocalConfigurationPtr cfg1, NeedleProblemHelperPtr helper);
    VectorXd operator()(const VectorXd& a) const;
    int outputSize() const;
  };

  struct NeedleProblemHelper : public boost::enable_shared_from_this<NeedleProblemHelper> {
    // Formulation flag
    enum Formulation { Form1 = 1, Form2 = 2 };
    enum CurvatureConstraint { ConstantRadius = 1, BoundedRadius = 2 };
    enum Method { Colocation = 1, Shooting = 2 };
    enum CurvatureFormulation { UseRadius = 1, UseCurvature = 2 };
    enum SpeedFormulation { ConstantSpeed = 1, VariableSpeed = 2 };
    // Config parameters
    VectorXd start;
    VectorXd goal;
    double coeff_rotation;
    double coeff_speed;
    int T;
    int n_dof;
    int formulation;
    int curvature_constraint;
    int speed_formulation;
    int method;
    int curvature_formulation;
    double r_min;
    vector<string> ignored_kinbody_names;
    double collision_dist_pen;
    double collision_coeff;
    double Delta_lb;
    KinBodyPtr robot;
    // Variables
    VarArray twistvars;
    VarArray phivars;
    VarArray curvature_or_radius_vars;
    VarArray Deltavars;
    Var Deltavar;
    // Local configurations
    vector<LocalConfigurationPtr> local_configs;

    void ConfigureProblem(OptProb& prob);
    void InitOptimizeVariables(BasicTrustRegionSQP& opt);
    void OptimizerCallback(OptProb*, DblVec& x);
    void ConfigureOptimizer(BasicTrustRegionSQP& opt);
    void CreateVariables(OptProb& prob);
    void InitLocalConfigurations(const KinBodyPtr robot, OptProb& prob);
    void InitTrajectory(OptProb& prob);
    void AddRotationCost(OptProb& prob);
    void AddSpeedCost(OptProb& prob);
    void AddStartConstraint(OptProb& prob);
    void AddGoalConstraint(OptProb& prob);
    void AddControlConstraint(OptProb& prob);
    void AddCollisionConstraint(OptProb& prob);
    Matrix4d TransformPose(const Matrix4d& pose, double phi, double Delta, double radius) const;
    double GetPhi(const DblVec& x, int i) const;
    double GetDelta(const DblVec& x, int i) const;
    double GetCurvatureOrRadius(const DblVec& x, int i) const;
    double GetCurvature(const DblVec& x, int i) const;
    double GetRadius(const DblVec& x, int i) const;
    #ifdef NEEDLE_TEST
    void checkAlignment(DblVec& x);
    #endif
  };

  struct TrajPlotter {
    vector<LocalConfigurationPtr> local_configs;
    VarArray vars;
    OSGViewerPtr viewer;
    TrajPlotter(const vector<LocalConfigurationPtr>& local_configs, const VarArray& vars);
    void OptimizerCallback(OptProb*, DblVec& x, NeedleProblemHelperPtr helper);
    KinBodyPtr GetGoalKinBody(NeedleProblemHelperPtr helper);
    void PlotBothTrajectories(OptProbPtr prob, const BasicTrustRegionSQP& opt, NeedleProblemHelperPtr helper);
  };
}
