#include "bsp/collision/utils.hpp"
#include "bsp/collision/belief_continuous_collision_collector.hpp"
#include "bsp/collision/sigma_hull_shape.hpp"
#include "bsp/collision/sigma_hull_cast_shape.hpp"

namespace BSPCollision {

  inline const KinBody::Link* getLink(const btCollisionObject* o) {
    return static_cast<const CollisionObjectWrapper*>(o)->m_link;
  }

  BeliefContinuousCollisionCollector::BeliefContinuousCollisionCollector(vector<BeliefCollision>& collisions, CollisionObjectWrapper* cow, BulletCollisionChecker* cc) :
      m_collisions(collisions), m_cow(cow), m_cc(cc) {}

  bool BeliefContinuousCollisionCollector::needsCollision(btBroadphaseProxy* proxy0) const {
    return (proxy0->m_collisionFilterGroup & m_collisionFilterMask)
        && (m_collisionFilterGroup & proxy0->m_collisionFilterMask)
        && m_cc->CanCollide(m_cow, static_cast<CollisionObjectWrapper*>(proxy0->m_clientObject));
  }

  btScalar BeliefContinuousCollisionCollector::addSingleResult(btManifoldPoint& cp,
      const btCollisionObjectWrapper* colObj0Wrap,int partId0,int index0,
      const btCollisionObjectWrapper* colObj1Wrap,int partId1,int index1) {
    if (cp.m_distance1 > m_cc->GetContactDistance()) return 0;
    const KinBody::Link* linkA = getLink(colObj0Wrap->getCollisionObject());
    const KinBody::Link* linkB = getLink(colObj1Wrap->getCollisionObject());

    m_collisions.push_back(BeliefCollision(linkA, linkB, toOR(cp.m_positionWorldOnA), toOR(cp.m_positionWorldOnB),
        toOR(cp.m_normalWorldOnB), cp.m_distance1));
    LOG_INFO("collide %s-%s", linkA->GetName().c_str(), linkB->GetName().c_str());
    //cout << "m position world on A: " << toVector(cp.m_positionWorldOnA).transpose() << endl;
    //cout << "m position world on B: " << toVector(cp.m_positionWorldOnB).transpose() << endl;
    bool castShapeIsFirst =  (colObj0Wrap->getCollisionObject() == m_cow);
    //if ((colObj0Wrap->getCollisionObject() == m_cow)) {
    //  cout << "cast shape is first" << endl;
    //} else if ((colObj1Wrap->getCollisionObject() == m_cow)) {
    //  cout << "cast shape is second" << endl;
    //} else {
    //  cout << "ERROR! CAST SHAPE IS NEITHER!!" << endl;
    //}

    btVector3 normalWorldFromCast = -(castShapeIsFirst ? 1 : -1) * cp.m_normalWorldOnB;
    //cout << "normal world from cast: " << toVector(normalWorldFromCast).transpose() << endl;
    const SigmaHullCastShape* shape = dynamic_cast<const SigmaHullCastShape*>(m_cow->getCollisionShape());
    assert(!!shape);

    BeliefCollision& collision = m_collisions.back();

    if (castShapeIsFirst) {
      swap(collision.ptA, collision.ptB);
      swap(collision.linkA, collision.linkB);
      collision.normalB2A *= -1;
    }

    //cout << "collision ptA: " << toVector(toBt(collision.ptA)).transpose() << endl;
    //cout << "collision ptB: " << toVector(toBt(collision.ptB)).transpose() << endl;

    vector<float> sup0, sup1;
    vector<btVector3> ptWorld0, ptWorld1;

    //cout << "shape transforms 0: " << endl;
    //for (auto& t : shape->m_t0i) {
    //  cout << toMatrix(t) << endl << endl;
    //}
    //cout << "shape transforms 1: " << endl;
    //for (auto& t : shape->m_t1i) {
    //  cout << toMatrix(t) << endl << endl;
    //}
    compute_points_and_supports(shape->m_shape, shape->m_t0i, normalWorldFromCast, m_cow, &sup0, &ptWorld0);
    compute_points_and_supports(shape->m_shape, shape->m_t1i, normalWorldFromCast, m_cow, &sup1, &ptWorld1);
    SigmaHullShape* shape0 = new SigmaHullShape(shape->m_shape, shape->m_t0i);
    SigmaHullShape* shape1 = new SigmaHullShape(shape->m_shape, shape->m_t1i);
    //vector<btTransform> trans_m_t1i;
    //for (int i = 0; i < shape->m_t1i.size(); ++i) {
    //  trans_m_t1i.push_back(shape->m_t1i[0].inverseTimes(shape->m_t1i[i]));
    //}
    //compute_points_and_supports(shape0, shape->m_t0i, normalWorldFromCast, m_cow, &sup0, &ptWorld0);
    //compute_points_and_supports(shape1, trans_m_t1i, normalWorldFromCast, m_cow, &sup1, &ptWorld1);

    btVector3 ptOnShape0 = shape0->localGetSupportingVertex(normalWorldFromCast);
    btVector3 ptOnShape1 = shape1->localGetSupportingVertex(normalWorldFromCast);
    //compute_points_and_supports(shape1, shape->m_t1i, normalWorldFromCast, m_cow, &sup1, &ptWorld1);

    delete shape0;
    delete shape1;

    //cout << "pt worlds 0: " << endl;
    //for (auto& pt : ptWorld0) {
    //  cout << toVector(pt).transpose() << endl;
    //}
    //cout << "pt worlds 1: " << endl;
    //for (auto& pt : ptWorld1) {
    //  cout << toVector(pt).transpose() << endl;
    //}

    vector<float> sups0, sups1;
    vector<btVector3> max_ptWorlds0, max_ptWorlds1;
    vector<int> instance_inds0, instance_inds1;
    compute_max_support_points(sup0, ptWorld0, &sups0, &max_ptWorlds0, &instance_inds0);
    compute_max_support_points(sup1, ptWorld1, &sups1, &max_ptWorlds1, &instance_inds1);

    assert(max_ptWorlds0.size()>0);
    assert(max_ptWorlds0.size()<4);
    assert(max_ptWorlds1.size()>0);
    assert(max_ptWorlds1.size()<4);

    const btVector3& ptOnCast = castShapeIsFirst ? cp.m_positionWorldOnA : cp.m_positionWorldOnB;
    
    computeSupportingWeights(max_ptWorlds0, ptOnShape0, collision.mi[0].alpha);
    computeSupportingWeights(max_ptWorlds1, ptOnShape1, collision.mi[1].alpha);
    //cout << "alpha size 0: " << collision.mi[0].alpha.size() << endl;
    //for (auto& i : collision.mi[0].alpha) {
    //  cout << "alpha 0: " << i << endl;
    //}
    //cout << "alpha size 1: " << collision.mi[1].alpha.size() << endl;
    //for (auto& i : collision.mi[1].alpha) {
    //  cout << "alpha 1: " << i << endl;
    //}
    //computeSupportingWeights(max_ptWorlds0, ptOnCast, collision.mi[0].alpha);
    //computeSupportingWeights(max_ptWorlds1, ptOnCast, collision.mi[1].alpha);

    collision.mi[0].instance_ind = instance_inds0;
    collision.mi[1].instance_ind = instance_inds1;

    const float SUPPORT_FUNC_TOLERANCE = .01;
    
    // TODO: this section is _definitely_ problematic. think hard about the math

    if (sups0[0] - sups1[0]> SUPPORT_FUNC_TOLERANCE) {
      collision.time = 0;
      collision.cctype = CCType_Time0;
    }
    else if (sups1[0] - sups0[0]> SUPPORT_FUNC_TOLERANCE) {
      collision.time = 1;
      collision.cctype = CCType_Time1;
    }
    else {
      float l0c = (ptOnCast - max_ptWorlds0[0]).length(), 
            l1c = (ptOnCast - max_ptWorlds1[0]).length();
      //cout << "max point worlds 0: " << endl;
      //for (auto& pt : max_ptWorlds0) {
      //  cout << toVector(pt).transpose() << endl;
      //}
      //cout << "max point worlds 1: " << endl;
      //for (auto& pt : max_ptWorlds1) {
      //  cout << toVector(pt).transpose() << endl;
      //}
      collision.ptB = toOR(max_ptWorlds0[0]);
      collision.ptB1 = toOR(max_ptWorlds1[0]);
      collision.cctype = CCType_Between;
      const float LENGTH_TOLERANCE = .001;
      if ( l0c + l1c < LENGTH_TOLERANCE) {
        collision.time = .5;
      }
      else {
        collision.time = l0c/(l0c + l1c); 
      }
    }
    //cout << "collision ptA: " << toVector(toBt(collision.ptA)).transpose() << endl;
    //cout << "collision ptB: " << toVector(toBt(collision.ptB)).transpose() << endl;
    //cout << "collision ptB1: " << toVector(toBt(collision.ptB1)).transpose() << endl;
    //cout << "collision time: " << collision.time << endl;
    return 1;
  }
}
