#include "trajopt/collision_avoidance.hpp"
#include "trajopt/rave_utils.hpp"
#include "trajopt/utils.hpp"
#include "sco/expr_vec_ops.hpp"
#include "sco/expr_ops.hpp"
#include "sco/sco_common.hpp"
#include <boost/foreach.hpp>
#include "utils/eigen_conversions.hpp"
#include "sco/modeling_utils.hpp"
#include "utils/stl_to_string.hpp"
using namespace OpenRAVE;
using namespace sco;
using namespace util;
using namespace std;

namespace trajopt {


void CollisionsToDistances(const vector<Collision>& collisions, const Link2Int& m_link2ind,
    DblVec& dists, DblVec& weights, NamePairs& bodyNames) {
  // Note: this checking (that the links are in the list we care about) is probably unnecessary
  // since we're using LinksVsAll
  dists.reserve(dists.size() + collisions.size());
  weights.reserve(weights.size() + collisions.size());
  BOOST_FOREACH(const Collision& col, collisions) {
    Link2Int::const_iterator itA = m_link2ind.find(col.linkA);
    Link2Int::const_iterator itB = m_link2ind.find(col.linkB);
    if (itA != m_link2ind.end() || itB != m_link2ind.end()) {
      dists.push_back(col.distance);
      weights.push_back(col.weight);
      bodyNames.push_back(pair<string, string>(col.linkA->GetParent()->GetName(), col.linkB->GetParent()->GetName()));
    }
  }
}

void CollisionsToDistanceExpressions(const vector<Collision>& collisions, RobotAndDOF& rad,
    const Link2Int& m_link2ind, const VarVector& m_vars, const DblVec& dofvals, vector<AffExpr>& exprs, DblVec& weights, NamePairs& bodyNames) {

  exprs.reserve(exprs.size() + collisions.size());
  weights.reserve(weights.size() + collisions.size());
  rad.SetDOFValues(dofvals); // since we'll be calculating jacobians
  BOOST_FOREACH(const Collision& col, collisions) {
    AffExpr dist(col.distance);
    Link2Int::const_iterator itA = m_link2ind.find(col.linkA);
    if (itA != m_link2ind.end()) {
      VectorXd dist_grad = toVector3d(col.normalB2A).transpose()*rad.PositionJacobian(itA->second, col.ptA);
      exprInc(dist, varDot(dist_grad, m_vars));
      exprInc(dist, -dist_grad.dot(toVectorXd(dofvals)));
    }
    Link2Int::const_iterator itB = m_link2ind.find(col.linkB);
    if (itB != m_link2ind.end()) {
      VectorXd dist_grad = -toVector3d(col.normalB2A).transpose()*rad.PositionJacobian(itB->second, col.ptB);
      exprInc(dist, varDot(dist_grad, m_vars));
      exprInc(dist, -dist_grad.dot(toVectorXd(dofvals)));
    }
    if (itA != m_link2ind.end() || itB != m_link2ind.end()) {
      exprs.push_back(dist);
      weights.push_back(col.weight);
      bodyNames.push_back(pair<string, string>(col.linkA->GetParent()->GetName(), col.linkB->GetParent()->GetName()));
    }
  }
  RAVELOG_DEBUG("%i distance expressions\n", exprs.size());
}

void CollisionEvaluator::GetCollisionsCached(const DblVec& x, vector<Collision>& collisions) {
  double key = vecSum(x);
  vector<Collision>* it = m_cache.get(key);
  if (it != NULL) {
    RAVELOG_DEBUG("using cached collision check\n");
    collisions = *it;
  }
  else {
    RAVELOG_DEBUG("not using cached collision check\n");
    CalcCollisions(x, collisions);
    m_cache.put(key, collisions);
  }
}

SingleTimestepCollisionEvaluator::SingleTimestepCollisionEvaluator(RobotAndDOFPtr rad, const VarVector& vars) :
  m_env(rad->GetRobot()->GetEnv()),
  m_cc(CollisionChecker::GetOrCreate(*m_env)),
  m_rad(rad),
  m_vars(vars),
  m_link2ind(),
  m_links() {
  RobotBasePtr robot = rad->GetRobot();
  const vector<KinBody::LinkPtr>& robot_links = robot->GetLinks();
  vector<KinBody::LinkPtr> links;
  vector<int> inds;
  rad->GetAffectedLinks(m_links, true, inds);
  for (int i=0; i < m_links.size(); ++i) {
    m_link2ind[m_links[i].get()] = inds[i];
  }
}


void SingleTimestepCollisionEvaluator::CalcCollisions(const DblVec& x, vector<Collision>& collisions) {
  DblVec dofvals = getDblVec(x, m_vars);
  m_rad->SetDOFValues(dofvals);
  m_cc->LinksVsAll(m_links, collisions);
}

void SingleTimestepCollisionEvaluator::CalcDists(const DblVec& x, DblVec& dists, DblVec& weights, NamePairs& bodyNames) {
  vector<Collision> collisions;
  GetCollisionsCached(x, collisions);
  CollisionsToDistances(collisions, m_link2ind, dists, weights, bodyNames);
}

void SingleTimestepCollisionEvaluator::CalcDistExpressions(const DblVec& x, vector<AffExpr>& exprs, DblVec& weights, NamePairs& bodyNames) {
  vector<Collision> collisions;
  GetCollisionsCached(x, collisions);
  DblVec dofvals = getDblVec(x, m_vars);
  CollisionsToDistanceExpressions(collisions, *m_rad, m_link2ind, m_vars, dofvals, exprs, weights, bodyNames);
}

////////////////////////////////////////

CastCollisionEvaluator::CastCollisionEvaluator(RobotAndDOFPtr rad, const VarVector& vars0, const VarVector& vars1) :
  m_env(rad->GetRobot()->GetEnv()),
  m_cc(CollisionChecker::GetOrCreate(*m_env)),
  m_rad(rad),
  m_vars0(vars0),
  m_vars1(vars1),
  m_link2ind(),
  m_links() {
  RobotBasePtr robot = rad->GetRobot();
  const vector<KinBody::LinkPtr>& robot_links = robot->GetLinks();
  vector<KinBody::LinkPtr> links;
  vector<int> inds;
  rad->GetAffectedLinks(m_links, true, inds);
  for (int i=0; i < m_links.size(); ++i) {
    m_link2ind[m_links[i].get()] = inds[i];
  }
}

void CastCollisionEvaluator::CalcCollisions(const DblVec& x, vector<Collision>& collisions) {
  DblVec dofvals0 = getDblVec(x, m_vars0);
  DblVec dofvals1 = getDblVec(x, m_vars1);
  m_rad->SetDOFValues(dofvals0);
  m_cc->CastVsAll(*m_rad, m_links, dofvals0, dofvals1, collisions);
}
void CastCollisionEvaluator::CalcDistExpressions(const DblVec& x, vector<AffExpr>& exprs, DblVec& weights, NamePairs& bodyNames) {
  vector<Collision> collisions;
  GetCollisionsCached(x, collisions);
  DblVec dofvals = getDblVec(x, m_vars0);
  CollisionsToDistanceExpressions(collisions, *m_rad, m_link2ind, m_vars0, dofvals, exprs, weights, bodyNames);
  dofvals = getDblVec(x, m_vars1);
  CollisionsToDistanceExpressions(collisions, *m_rad, m_link2ind, m_vars1, dofvals, exprs, weights, bodyNames);
}
void CastCollisionEvaluator::CalcDists(const DblVec& x, DblVec& exprs, DblVec& weights, NamePairs& bodyNames) {
  vector<Collision> collisions;
  GetCollisionsCached(x, collisions);
  DblVec dofvals = getDblVec(x, m_vars0);
  CollisionsToDistances(collisions, m_link2ind, exprs, weights, bodyNames);
  dofvals = getDblVec(x, m_vars1);
  CollisionsToDistances(collisions, m_link2ind, exprs, weights, bodyNames);
}


//////////////////////////////////////////



typedef OpenRAVE::RaveVector<float> RaveVectorf;

void PlotCollisions(const std::vector<Collision>& collisions, OR::EnvironmentBase& env, vector<OR::GraphHandlePtr>& handles, double safe_dist) {
  BOOST_FOREACH(const Collision& col, collisions) {
    RaveVectorf color;
    if (col.distance < 0) color = RaveVectorf(1,0,0,1);
    else if (col.distance < safe_dist) color = RaveVectorf(1,1,0,1);
    else color = RaveVectorf(0,1,0,1);
    handles.push_back(env.drawarrow(col.ptA, col.ptB, .0025, color));
  }
}

CollisionCost::CollisionCost(double dist_pen, double coeff, RobotAndDOFPtr rad, const VarVector& vars) :
    Cost("collision"),
    m_calc(new SingleTimestepCollisionEvaluator(rad, vars)), m_dist_pen(dist_pen), m_coeff(coeff)
{}

CollisionCost::CollisionCost(double dist_pen, double coeff, RobotAndDOFPtr rad, const VarVector& vars0, const VarVector& vars1) :
    Cost("cast_collision"),
    m_calc(new CastCollisionEvaluator(rad, vars0, vars1)), m_dist_pen(dist_pen), m_coeff(coeff)
{}


ConvexObjectivePtr CollisionCost::convex(const vector<double>& x, Model* model) {
  ConvexObjectivePtr out(new ConvexObjective(model));
  vector<AffExpr> exprs;
  DblVec weights;
  NamePairs bodyNames;
  m_calc->CalcDistExpressions(x, exprs, weights, bodyNames);
  for (int i=0; i < exprs.size(); ++i) {
    AffExpr viol = exprSub(AffExpr(m_dist_pen), exprs[i]);
    out->addHinge(viol, m_coeff*weights[i]);
  }
  return out;
}
double CollisionCost::value(const vector<double>& x) {
  DblVec dists, weights;
  NamePairs bodyNames;
  m_calc->CalcDists(x, dists, weights, bodyNames);
  double out = 0;
  for (int i=0; i < dists.size(); ++i) {
    out += pospart(m_dist_pen - dists[i]) * m_coeff * weights[i];
  }
  return out;
}

void CollisionCost::Plot(const DblVec& x, OR::EnvironmentBase& env, std::vector<OR::GraphHandlePtr>& handles) {
  vector<Collision> collisions;
  m_calc->GetCollisionsCached(x, collisions);
  PlotCollisions(collisions, env, handles, m_dist_pen);
}

CollisionTaggedCost::CollisionTaggedCost(const Str2Dbl& tag2dist_pen, const Str2Dbl& tag2coeff, RobotAndDOFPtr rad, const VarVector& vars) :
    Cost("collision"),
    m_calc(new SingleTimestepCollisionEvaluator(rad, vars)), m_tag2dist_pen(tag2dist_pen), m_tag2coeff(tag2coeff)
{}


CollisionTaggedCost::CollisionTaggedCost(const Str2Dbl& tag2dist_pen, const Str2Dbl& tag2coeff, RobotAndDOFPtr rad, const VarVector& vars0, const VarVector& vars1) :
    Cost("cast_collision"),
    m_calc(new CastCollisionEvaluator(rad, vars0, vars1)), m_tag2dist_pen(tag2dist_pen), m_tag2coeff(tag2coeff)
{}

ConvexObjectivePtr CollisionTaggedCost::convex(const vector<double>& x, Model* model) {
  ConvexObjectivePtr out(new ConvexObjective(model));
  vector<AffExpr> exprs;
  DblVec weights;
  NamePairs bodyNames;
  m_calc->CalcDistExpressions(x, exprs, weights, bodyNames);
  for (int i=0; i < exprs.size(); ++i) {
    cout << "bodyNames: " << bodyNames[i].first << " " << bodyNames[i].second << endl;
    double dist_pen = min(m_tag2dist_pen[bodyNames[i].first], m_tag2dist_pen[bodyNames[i].second]);
    double coeff = (m_tag2coeff[bodyNames[i].first] + m_tag2coeff[bodyNames[i].second]);
    cout << "dist pen: " << dist_pen << endl;
    cout << "coeff: " << coeff << endl;
    AffExpr viol = exprSub(AffExpr(dist_pen), exprs[i]);
    out->addHinge(viol, coeff*weights[i]);
  }
  return out;
}

double CollisionTaggedCost::value(const vector<double>& x) {
  DblVec dists, weights;
  NamePairs bodyNames;
  m_calc->CalcDists(x, dists, weights, bodyNames);
  double out = 0;
  for (int i=0; i < dists.size(); ++i) {
    double dist_pen = min(m_tag2dist_pen[bodyNames[i].first], m_tag2dist_pen[bodyNames[i].second]);
    double coeff = (m_tag2coeff[bodyNames[i].first] + m_tag2coeff[bodyNames[i].second]);
    out += pospart(dist_pen - dists[i]) * coeff * weights[i];
  }
  return out;
}

void CollisionTaggedCost::Plot(const DblVec& x, OR::EnvironmentBase& env, std::vector<OR::GraphHandlePtr>& handles) {
  vector<Collision> collisions;
  m_calc->GetCollisionsCached(x, collisions);
  BOOST_FOREACH(const Collision& col, collisions) {
    double safe_dist = min(m_tag2dist_pen[col.linkA->GetName()], m_tag2dist_pen[col.linkB->GetName()]);
    RaveVectorf color;
    if (col.distance < 0) color = RaveVectorf(1,0,0,1);
    else if (col.distance < safe_dist) color = RaveVectorf(1,1,0,1);
    else color = RaveVectorf(0,1,0,1);
    handles.push_back(env.drawarrow(col.ptA, col.ptB, .0025, color));
  }
}

}
