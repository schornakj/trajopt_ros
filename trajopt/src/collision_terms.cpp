#include <trajopt_utils/macros.h>
TRAJOPT_IGNORE_WARNINGS_PUSH
#include <boost/functional/hash.hpp>
#include <tesseract_kinematics/core/forward_kinematics.h>
#include <tesseract_kinematics/core/utils.h>
TRAJOPT_IGNORE_WARNINGS_POP

#include <trajopt/collision_terms.hpp>
#include <trajopt/utils.hpp>
#include <trajopt_sco/expr_ops.hpp>
#include <trajopt_sco/expr_vec_ops.hpp>
#include <trajopt_sco/modeling_utils.hpp>
#include <trajopt_sco/sco_common.hpp>
#include <trajopt_utils/eigen_conversions.hpp>
#include <trajopt_utils/logging.hpp>
#include <trajopt_utils/stl_to_string.hpp>

namespace trajopt
{
void CollisionsToDistances(const tesseract_collision::ContactResultVector& dist_results, DblVec& dists)
{
  dists.clear();
  dists.reserve(dist_results.size());
  for (const auto& dist_result : dist_results)
    dists.push_back(dist_result.distance);
}

void DebugPrintInfo(const tesseract_collision::ContactResult& res,
                    const Eigen::VectorXd& dist_grad_A,
                    const Eigen::VectorXd& dist_grad_B,
                    const Eigen::VectorXd& dof_vals,
                    bool header = false)
{
  if (header)
  {
    std::printf("\n");
    std::printf("DistanceResult| %30s | %30s | %6s | %6s, %6s, %6s | %6s, %6s, "
                "%6s | %6s, %6s, %6s | %6s, %6s, %6s | %6s, %6s, %6s | %10s %10s |",
                "LINK A",
                "LINK B",
                "DIST",
                "Nx",
                "Ny",
                "Nz",
                "PAx",
                "PAy",
                "PAz",
                "PBx",
                "PBy",
                "PBz",
                "LPAx",
                "LPAy",
                "LPAz",
                "LPBx",
                "LPBy",
                "LPBz",
                "CC TIME A",
                "CC TIME B");

    for (auto i = 0; i < dist_grad_A.size(); ++i)
    {
      if (i == dist_grad_A.size() - 1)
      {
        std::printf(" %6s |", ("dA" + std::to_string(i)).c_str());
      }
      else
      {
        std::printf(" %6s,", ("dA" + std::to_string(i)).c_str());
      }
    }

    for (auto i = 0; i < dist_grad_B.size(); ++i)
    {
      if (i == dist_grad_B.size() - 1)
      {
        std::printf(" %6s |", ("dB" + std::to_string(i)).c_str());
      }
      else
      {
        std::printf(" %6s,", ("dB" + std::to_string(i)).c_str());
      }
    }

    for (auto i = 0; i < dof_vals.size(); ++i)
    {
      if (i == dof_vals.size() - 1)
      {
        std::printf(" %6s |", ("J" + std::to_string(i)).c_str());
      }
      else
      {
        std::printf(" %6s,", ("J" + std::to_string(i)).c_str());
      }
    }

    std::printf("\n");
  }

  std::printf("DistanceResult| %30s | %30s | %6.3f | %6.3f, %6.3f, %6.3f | "
              "%6.3f, %6.3f, %6.3f | %6.3f, %6.3f, %6.3f | %6.3f, "
              "%6.3f, %6.3f | %6.3f, %6.3f, %6.3f | %10.3f %10.3f |",
              res.link_names[0].c_str(),
              res.link_names[1].c_str(),
              res.distance,
              res.normal(0),
              res.normal(1),
              res.normal(2),
              res.nearest_points[0](0),
              res.nearest_points[0](1),
              res.nearest_points[0](2),
              res.nearest_points[1](0),
              res.nearest_points[1](1),
              res.nearest_points[1](2),
              res.nearest_points_local[0](0),
              res.nearest_points_local[0](1),
              res.nearest_points_local[0](2),
              res.nearest_points_local[1](0),
              res.nearest_points_local[1](1),
              res.nearest_points_local[1](2),
              res.cc_time[0],
              res.cc_time[1]);

  for (auto i = 0; i < dist_grad_A.size(); ++i)
  {
    if (i == dist_grad_A.size() - 1)
    {
      std::printf(" %6.3f |", dist_grad_A(i));
    }
    else
    {
      std::printf(" %6.3f,", dist_grad_A(i));
    }
  }

  for (auto i = 0; i < dist_grad_B.size(); ++i)
  {
    if (i == dist_grad_B.size() - 1)
    {
      std::printf(" %6.3f |", dist_grad_B(i));
    }
    else
    {
      std::printf(" %6.3f,", dist_grad_B(i));
    }
  }

  for (auto i = 0; i < dof_vals.size(); ++i)
  {
    if (i == dof_vals.size() - 1)
    {
      std::printf(" %6.3f |", dof_vals(i));
    }
    else
    {
      std::printf(" %6.3f,", dof_vals(i));
    }
  }

  std::printf("\n");
}

void CollisionsToDistanceExpressions(sco::AffExprVector& exprs,
                                     const tesseract_collision::ContactResultVector& dist_results,
                                     const tesseract_kinematics::ForwardKinematics::ConstPtr& manip,
                                     const tesseract_environment::AdjacencyMap::ConstPtr& adjacency_map,
                                     const Eigen::Isometry3d& world_to_base,
                                     const sco::VarVector& vars,
                                     const DblVec& x,
                                     bool isTimestep1)
{
  Eigen::VectorXd dofvals = sco::getVec(x, vars);

  // All collision data is in world corrdinate system. This provides the
  // transfrom for converting data between world frame and manipulator
  // frame.

  exprs.clear();
  exprs.reserve(dist_results.size());
  for (const auto& res : dist_results)
  {
    sco::AffExpr dist(0);

    Eigen::VectorXd dist_grad_a, dist_grad_b;
    tesseract_environment::AdjacencyMapPair::ConstPtr itA = adjacency_map->getLinkMapping(res.link_names[0]);
    if (itA != nullptr)
    {
      Eigen::MatrixXd jac;
      jac.resize(6, manip->numJoints());

      // Calculate Jacobian
      manip->calcJacobian(jac, dofvals, itA->link_name);

      // Need to change the base and ref point of the jacobian.
      // When changing ref point you must provide a vector from the current ref
      // point to the new ref point.
      double scale = 1;
      Eigen::Isometry3d link_transform = res.transform[0];
      if (res.cc_type[0] != tesseract_collision::ContinuousCollisionType::CCType_None)
      {
        assert(res.cc_time[0] >= 0.0 && res.cc_time[0] <= 1.0);
        scale = (isTimestep1) ? res.cc_time[0] : (1 - res.cc_time[0]);
        link_transform = (isTimestep1) ? res.cc_transform[0] : res.transform[0];
      }
      tesseract_kinematics::jacobianChangeBase(jac, world_to_base);
      tesseract_kinematics::jacobianChangeRefPoint(
          jac, (link_transform * itA->transform.inverse()).linear() * (itA->transform * res.nearest_points_local[0]));

      // Eigen::Isometry3d test_link_transform, temp1, temp2;
      // manip->calcFwdKin(test_link_transform, dofvals, itA->link_name);
      // temp1 = world_to_base * test_link_transform;
      // temp2 = link_transform * itA->transform.inverse();
      // assert(temp1.isApprox(temp2, 0.0001));

      // Eigen::MatrixXd jac_test;
      // jac_test.resize(6, manip->numJoints());
      // tesseract_kinematics::numericalJacobian(jac_test, world_to_base, *manip, dofvals, itA->link_name,
      // res.nearest_points_local[0]); bool check = jac.isApprox(jac_test, 1e-3); assert(check == true);

      dist_grad_a = -res.normal.transpose() * jac.topRows(3);
      sco::exprInc(dist, sco::varDot(scale * dist_grad_a, vars));
      sco::exprInc(dist, scale * -dist_grad_a.dot(dofvals));
    }

    tesseract_environment::AdjacencyMapPair::ConstPtr itB = adjacency_map->getLinkMapping(res.link_names[1]);
    if (itB != nullptr)
    {
      Eigen::MatrixXd jac;
      jac.resize(6, manip->numJoints());

      // Calculate Jacobian
      manip->calcJacobian(jac, dofvals, itB->link_name);

      // Need to change the base and ref point of the jacobian.
      // When changing ref point you must provide a vector from the current ref
      // point to the new ref point.
      double scale = 1;
      Eigen::Isometry3d link_transform = res.transform[1];
      if (res.cc_type[1] != tesseract_collision::ContinuousCollisionType::CCType_None)
      {
        assert(res.cc_time[1] >= 0.0 && res.cc_time[1] <= 1.0);
        scale = (isTimestep1) ? res.cc_time[1] : (1 - res.cc_time[1]);
        link_transform = (isTimestep1) ? res.cc_transform[1] : res.transform[1];
      }
      tesseract_kinematics::jacobianChangeBase(jac, world_to_base);
      tesseract_kinematics::jacobianChangeRefPoint(
          jac, (link_transform * itB->transform.inverse()).linear() * (itB->transform * res.nearest_points_local[1]));

      // Eigen::Isometry3d test_link_transform, temp1, temp2;
      // manip->calcFwdKin(test_link_transform, dofvals, itB->link_name);
      // temp1 = world_to_base * test_link_transform;
      // temp2 = link_transform * itB->transform.inverse();
      // assert(temp1.isApprox(temp2, 0.0001));

      // Eigen::MatrixXd jac_test;
      // jac_test.resize(6, manip->numJoints());
      // tesseract_kinematics::numericalJacobian(jac_test, world_to_base, *manip, dofvals, itB->link_name,
      // res.nearest_points_local[1]); bool check = jac.isApprox(jac_test, 1e-3); assert(check == true);

      dist_grad_b = res.normal.transpose() * jac.topRows(3);
      sco::exprInc(dist, sco::varDot(scale * dist_grad_b, vars));
      sco::exprInc(dist, scale * -dist_grad_b.dot(dofvals));
    }
    // DebugPrintInfo(res, dist_grad_a, dist_grad_b, dofvals, &res == &(dist_results.front()));

    if (itA != nullptr || itB != nullptr)
    {
      exprs.push_back(dist);
    }
  }
}

CollisionEvaluator::CollisionEvaluator(tesseract_kinematics::ForwardKinematics::ConstPtr manip,
                                       tesseract_environment::Environment::ConstPtr env,
                                       tesseract_environment::AdjacencyMap::ConstPtr adjacency_map,
                                       const Eigen::Isometry3d& world_to_base,
                                       SafetyMarginData::ConstPtr safety_margin_data,
                                       tesseract_collision::ContactTestType contact_test_type,
                                       double longest_valid_segment_length,
                                       double safety_margin_buffer)
  : manip_(std::move(manip))
  , env_(std::move(env))
  , adjacency_map_(std::move(adjacency_map))
  , world_to_base_(world_to_base)
  , safety_margin_data_(std::move(safety_margin_data))
  , safety_margin_buffer_(safety_margin_buffer)
  , contact_test_type_(contact_test_type)
  , longest_valid_segment_length_(longest_valid_segment_length)
  , state_solver_(env_->getStateSolver())
{
}

void CollisionEvaluator::CalcDists(const DblVec& x, DblVec& dists)
{
  tesseract_collision::ContactResultVector dist_results;
  GetCollisionsCached(x, dist_results);
  CollisionsToDistances(dist_results, dists);
}

inline size_t hash(const DblVec& x) { return boost::hash_range(x.begin(), x.end()); }
void CollisionEvaluator::GetCollisionsCached(const DblVec& x, tesseract_collision::ContactResultVector& dist_results)
{
  size_t key = hash(sco::getDblVec(x, GetVars()));
  tesseract_collision::ContactResultVector* it = m_cache.get(key);
  if (it != nullptr)
  {
    LOG_DEBUG("using cached collision check\n");
    dist_results = *it;
  }
  else
  {
    LOG_DEBUG("not using cached collision check\n");
    CalcCollisions(x, dist_results);
    m_cache.put(key, dist_results);
  }
}

void CollisionEvaluator::CalcDistExpressionsStartFree(const DblVec& x, sco::AffExprVector& exprs)
{
  tesseract_collision::ContactResultVector dist_results;
  GetCollisionsCached(x, dist_results);

  sco::AffExprVector exprs0;
  CollisionsToDistanceExpressions(exprs0, dist_results, manip_, adjacency_map_, world_to_base_, vars0_, x, false);

  exprs.resize(exprs0.size());
  assert(exprs0.size() == dist_results.size());
  for (std::size_t i = 0; i < exprs0.size(); ++i)
  {
    exprs[i] = sco::AffExpr(dist_results[i].distance);
    sco::exprInc(exprs[i], exprs0[i]);
    sco::cleanupAff(exprs[i]);
  }
}

void CollisionEvaluator::CalcDistExpressionsEndFree(const DblVec& x, sco::AffExprVector& exprs)
{
  tesseract_collision::ContactResultVector dist_results;
  GetCollisionsCached(x, dist_results);

  sco::AffExprVector exprs1;
  CollisionsToDistanceExpressions(exprs1, dist_results, manip_, adjacency_map_, world_to_base_, vars1_, x, true);

  exprs.resize(exprs1.size());
  assert(exprs1.size() == dist_results.size());
  for (std::size_t i = 0; i < exprs1.size(); ++i)
  {
    exprs[i] = sco::AffExpr(dist_results[i].distance);
    sco::exprInc(exprs[i], exprs1[i]);
    sco::cleanupAff(exprs[i]);
  }
}

void CollisionEvaluator::CalcDistExpressionsBothFree(const DblVec& x, sco::AffExprVector& exprs)
{
  tesseract_collision::ContactResultVector dist_results;
  GetCollisionsCached(x, dist_results);

  sco::AffExprVector exprs0, exprs1;
  CollisionsToDistanceExpressions(exprs0, dist_results, manip_, adjacency_map_, world_to_base_, vars0_, x, false);
  CollisionsToDistanceExpressions(exprs1, dist_results, manip_, adjacency_map_, world_to_base_, vars1_, x, true);

  exprs.resize(exprs0.size());
  assert(exprs0.size() == dist_results.size());
  for (std::size_t i = 0; i < exprs0.size(); ++i)
  {
    exprs[i] = sco::AffExpr(dist_results[i].distance);
    sco::exprInc(exprs[i], exprs0[i]);
    sco::exprInc(exprs[i], exprs1[i]);
    sco::cleanupAff(exprs[i]);
  }
}

void CollisionEvaluator::processInterpolatedCollisionResults(
    std::vector<tesseract_collision::ContactResultMap>& contacts_vector,
    tesseract_collision::ContactResultMap& contact_results) const
{
  // If contact is found the actual dt between the original two state must be recalculated based on where it
  // occured in the subtrajectory. Also the cc_type must also be recalculated but does not appear to be used
  // currently by trajopt.
  const std::vector<std::string>& active_links = adjacency_map_->getActiveLinkNames();
  double dt = 1.0 / static_cast<double>(contacts_vector.size() - 1);
  for (size_t i = 0; i < contacts_vector.size(); ++i)
  {
    for (auto& pair : contacts_vector[i])
    {
      auto p = contact_results.find(pair.first);
      const Eigen::Vector2d& data = getSafetyMarginData()->getPairSafetyMarginData(pair.first.first, pair.first.second);

      // Update cc_time and cc_type
      for (auto& r : pair.second)
      {
        if (std::find(active_links.begin(), active_links.end(), r.link_names[0]) != active_links.end())
        {
          r.cc_time[0] = (static_cast<double>(i) * dt);
          if (i == 0 && r.cc_type[0] == tesseract_collision::ContinuousCollisionType::CCType_Time0)
            r.cc_type[0] = tesseract_collision::ContinuousCollisionType::CCType_Time0;
          else if (i == (contacts_vector.size() - 1) &&
                   r.cc_type[0] == tesseract_collision::ContinuousCollisionType::CCType_Time1)
            r.cc_type[0] = tesseract_collision::ContinuousCollisionType::CCType_Time1;
          else
            r.cc_type[0] = tesseract_collision::ContinuousCollisionType::CCType_Between;
        }

        if (std::find(active_links.begin(), active_links.end(), r.link_names[1]) != active_links.end())
        {
          r.cc_time[1] = (static_cast<double>(i) * dt);
          if (i == 0 && r.cc_type[1] == tesseract_collision::ContinuousCollisionType::CCType_Time0)
            r.cc_type[1] = tesseract_collision::ContinuousCollisionType::CCType_Time0;
          else if (i == (contacts_vector.size() - 1) &&
                   r.cc_type[1] == tesseract_collision::ContinuousCollisionType::CCType_Time1)
            r.cc_type[1] = tesseract_collision::ContinuousCollisionType::CCType_Time1;
          else
            r.cc_type[1] = tesseract_collision::ContinuousCollisionType::CCType_Between;
        }
      }

      // Dont include contacts at the fixed state
      removeInvalidContactResults(pair.second, data);

      // If the contact pair does not exist in contact_results add it
      if (p == contact_results.end() && !pair.second.empty())
      {
        contact_results[pair.first] = pair.second;
      }
      else
      {
        // Note: Must include all contacts throughout the trajectory so the optimizer has all the information
        //      to understand how to adjust the start and end state to move it out of collision. Originally tried
        //      keeping the worst case only but ran into edge cases where this does not work in the units tests.

        // If it exists then add addition contacts to the contact_results pair
        for (auto& r : pair.second)
          p->second.push_back(r);
      }
    }
  }
}

void CollisionEvaluator::removeInvalidContactResults(tesseract_collision::ContactResultVector& contact_results,
                                                     const Eigen::Vector2d& pair_data) const
{
  auto end = std::remove_if(
      contact_results.begin(), contact_results.end(), [=, &pair_data](const tesseract_collision::ContactResult& r) {
        switch (evaluator_type_)
        {
          case CollisionExpressionEvaluatorType::START_FREE_END_FREE:
          {
            break;
          }
          case CollisionExpressionEvaluatorType::START_FIXED_END_FREE:
          {
            if (r.cc_type[0] == tesseract_collision::ContinuousCollisionType::CCType_Time0)
              return true;

            if (r.cc_type[1] == tesseract_collision::ContinuousCollisionType::CCType_Time0)
              return true;

            break;
          }
          case CollisionExpressionEvaluatorType::START_FREE_END_FIXED:
          {
            if (r.cc_type[0] == tesseract_collision::ContinuousCollisionType::CCType_Time1)
              return true;

            if (r.cc_type[1] == tesseract_collision::ContinuousCollisionType::CCType_Time1)
              return true;

            break;
          }
        };

        return (!((pair_data[0] + safety_margin_buffer_) > r.distance));
      });

  contact_results.erase(end, contact_results.end());
}

SingleTimestepCollisionEvaluator::SingleTimestepCollisionEvaluator(
    tesseract_kinematics::ForwardKinematics::ConstPtr manip,
    tesseract_environment::Environment::ConstPtr env,
    tesseract_environment::AdjacencyMap::ConstPtr adjacency_map,
    const Eigen::Isometry3d& world_to_base,
    SafetyMarginData::ConstPtr safety_margin_data,
    tesseract_collision::ContactTestType contact_test_type,
    sco::VarVector vars,
    double safety_margin_buffer)
  : CollisionEvaluator(std::move(manip),
                       std::move(env),
                       std::move(adjacency_map),
                       world_to_base,
                       std::move(safety_margin_data),
                       contact_test_type,
                       0,
                       safety_margin_buffer)
{
  vars0_ = std::move(vars);

  contact_manager_ = env_->getDiscreteContactManager();
  contact_manager_->setActiveCollisionObjects(adjacency_map_->getActiveLinkNames());
  contact_manager_->setContactDistanceThreshold(safety_margin_data_->getMaxSafetyMargin() + safety_margin_buffer_);
}

void SingleTimestepCollisionEvaluator::CalcCollisions(const DblVec& x,
                                                      tesseract_collision::ContactResultVector& dist_results)
{
  tesseract_collision::ContactResultMap contacts;
  tesseract_environment::EnvState::Ptr state = state_solver_->getState(manip_->getJointNames(), sco::getVec(x, vars0_));

  for (const auto& link_name : adjacency_map_->getActiveLinkNames())
    contact_manager_->setCollisionObjectsTransform(link_name, state->transforms[link_name]);

  contact_manager_->contactTest(contacts, contact_test_type_);

  for (auto& pair : contacts)
  {
    const Eigen::Vector2d& data = getSafetyMarginData()->getPairSafetyMarginData(pair.first.first, pair.first.second);
    auto end = std::remove_if(
        pair.second.begin(), pair.second.end(), [&data, this](const tesseract_collision::ContactResult& r) {
          return (!((data[0] + safety_margin_buffer_) > r.distance));
        });
    pair.second.erase(end, pair.second.end());
  }

  // Flatten results into a single vector
  tesseract_collision::flattenResults(std::move(contacts), dist_results);
}

void SingleTimestepCollisionEvaluator::CalcDistExpressions(const DblVec& x, sco::AffExprVector& exprs)
{
  tesseract_collision::ContactResultVector dist_results;
  GetCollisionsCached(x, dist_results);
  CollisionsToDistanceExpressions(exprs, dist_results, manip_, adjacency_map_, world_to_base_, vars0_, x, false);
  assert(dist_results.size() == exprs.size());

  for (std::size_t i = 0; i < exprs.size(); ++i)
    sco::exprInc(exprs[i], dist_results[i].distance);

  LOG_DEBUG("%ld distance expressions\n", exprs.size());
}

void SingleTimestepCollisionEvaluator::Plot(const tesseract_visualization::Visualization::Ptr& plotter, const DblVec& x)
{
  tesseract_collision::ContactResultVector dist_results;
  GetCollisionsCached(x, dist_results);
  Eigen::VectorXd dofvals = sco::getVec(x, vars0_);

  Eigen::VectorXd safety_distance(dist_results.size());
  for (auto i = 0u; i < dist_results.size(); ++i)
  {
    const tesseract_collision::ContactResult& res = dist_results[i];
    const Eigen::Vector2d& data = getSafetyMarginData()->getPairSafetyMarginData(res.link_names[0], res.link_names[1]);
    safety_distance[i] = data[0];

    tesseract_environment::AdjacencyMapPair::ConstPtr itA = adjacency_map_->getLinkMapping(res.link_names[0]);
    if (itA != nullptr)
    {
      Eigen::MatrixXd jac;
      Eigen::VectorXd dist_grad;
      Eigen::Isometry3d pose, pose2;
      jac.resize(6, manip_->numJoints());
      manip_->calcFwdKin(pose, dofvals, itA->link_name);
      pose = world_to_base_ * pose;

      Eigen::Vector3d local_link_point = pose.inverse() * res.nearest_points[0];

      manip_->calcJacobian(jac, dofvals, itA->link_name);
      tesseract_kinematics::jacobianChangeBase(jac, world_to_base_);
      tesseract_kinematics::jacobianChangeRefPoint(jac, pose.linear() * local_link_point);

      dist_grad = -res.normal.transpose() * jac.topRows(3);

      manip_->calcFwdKin(pose2, dofvals + dist_grad, itA->link_name);
      pose2 = world_to_base_ * pose2 * itA->transform;
      plotter->plotArrow(res.nearest_points[0], pose2 * local_link_point, Eigen::Vector4d(1, 1, 1, 1), 0.005);
    }
  }

  plotter->plotContactResults(adjacency_map_->getActiveLinkNames(), dist_results, safety_distance);
}

////////////////////////////////////////

DiscreteCollisionEvaluator::DiscreteCollisionEvaluator(tesseract_kinematics::ForwardKinematics::ConstPtr manip,
                                                       tesseract_environment::Environment::ConstPtr env,
                                                       tesseract_environment::AdjacencyMap::ConstPtr adjacency_map,
                                                       const Eigen::Isometry3d& world_to_base,
                                                       SafetyMarginData::ConstPtr safety_margin_data,
                                                       tesseract_collision::ContactTestType contact_test_type,
                                                       double longest_valid_segment_length,
                                                       sco::VarVector vars0,
                                                       sco::VarVector vars1,
                                                       CollisionExpressionEvaluatorType type,
                                                       double safety_margin_buffer)
  : CollisionEvaluator(std::move(manip),
                       std::move(env),
                       std::move(adjacency_map),
                       world_to_base,
                       std::move(safety_margin_data),
                       contact_test_type,
                       longest_valid_segment_length,
                       safety_margin_buffer)
{
  vars0_ = std::move(vars0);
  vars1_ = std::move(vars1);
  evaluator_type_ = type;

  contact_manager_ = env_->getDiscreteContactManager();
  contact_manager_->setActiveCollisionObjects(adjacency_map_->getActiveLinkNames());
  contact_manager_->setContactDistanceThreshold(safety_margin_data_->getMaxSafetyMargin() + safety_margin_buffer_);

  switch (evaluator_type_)
  {
    case CollisionExpressionEvaluatorType::START_FREE_END_FREE:
    {
      fn_ = std::bind(
          &DiscreteCollisionEvaluator::CalcDistExpressionsBothFree, this, std::placeholders::_1, std::placeholders::_2);
      break;
    }
    case CollisionExpressionEvaluatorType::START_FIXED_END_FREE:
    {
      fn_ = std::bind(
          &DiscreteCollisionEvaluator::CalcDistExpressionsEndFree, this, std::placeholders::_1, std::placeholders::_2);
      break;
    }
    case CollisionExpressionEvaluatorType::START_FREE_END_FIXED:
    {
      fn_ = std::bind(&DiscreteCollisionEvaluator::CalcDistExpressionsStartFree,
                      this,
                      std::placeholders::_1,
                      std::placeholders::_2);
      break;
    }
  };
}

void DiscreteCollisionEvaluator::CalcCollisions(const DblVec& x, tesseract_collision::ContactResultVector& dist_results)
{
  Eigen::VectorXd s0 = sco::getVec(x, vars0_);
  Eigen::VectorXd s1 = sco::getVec(x, vars1_);
  tesseract_collision::ContactResultMap contact_results;

  // The first step is to see if the distance between two states is larger than the longest valid segment. If larger
  // the collision checking is broken up into multiple casted collision checks such that each check is less then
  // the longest valid segment length.
  double dist = (s1 - s0).norm();
  long cnt = 2;
  if (dist > longest_valid_segment_length_)
  {
    // Calculate the number state to interpolate
    cnt = static_cast<long>(std::ceil(dist / longest_valid_segment_length_)) + 1;
  }

  // Get active link names
  const std::vector<std::string>& active_links = adjacency_map_->getActiveLinkNames();

  // Create interpolated trajectory between two states that satisfies the longest valid segment length.
  tesseract_common::TrajArray subtraj(cnt, s0.size());
  for (long i = 0; i < s0.size(); ++i)
    subtraj.col(i) = Eigen::VectorXd::LinSpaced(cnt, s0(i), s1(i));

  // Perform casted collision checking for sub trajectory and store results in contacts_vector
  std::vector<tesseract_collision::ContactResultMap> contacts_vector;
  contacts_vector.reserve(static_cast<size_t>(subtraj.rows()));
  bool contact_found = false;
  for (int i = 0; i < subtraj.rows(); ++i)
  {
    tesseract_collision::ContactResultMap contacts;
    tesseract_environment::EnvState::Ptr state0 = state_solver_->getState(manip_->getJointNames(), subtraj.row(i));

    for (const auto& link_name : active_links)
      contact_manager_->setCollisionObjectsTransform(link_name, state0->transforms[link_name]);

    contact_manager_->contactTest(contacts, contact_test_type_);
    if (!contacts.empty())
      contact_found = true;

    contacts_vector.push_back(contacts);
  }

  if (contact_found)
    processInterpolatedCollisionResults(contacts_vector, contact_results);

  // Flatten results
  tesseract_collision::flattenResults(std::move(contact_results), dist_results);
}

void DiscreteCollisionEvaluator::CalcDistExpressions(const DblVec& x, sco::AffExprVector& exprs) { fn_(x, exprs); }

void DiscreteCollisionEvaluator::Plot(const tesseract_visualization::Visualization::Ptr& plotter, const DblVec& x)
{
  tesseract_collision::ContactResultVector dist_results;
  GetCollisionsCached(x, dist_results);
  Eigen::VectorXd dofvals0 = sco::getVec(x, vars0_);
  Eigen::VectorXd dofvals1 = sco::getVec(x, vars1_);

  Eigen::VectorXd safety_distance(dist_results.size());
  for (auto i = 0u; i < dist_results.size(); ++i)
  {
    tesseract_collision::ContactResult& res = dist_results[i];
    const Eigen::Vector2d& data = getSafetyMarginData()->getPairSafetyMarginData(res.link_names[0], res.link_names[1]);
    safety_distance[i] = data[0];

    tesseract_environment::AdjacencyMapPair::ConstPtr itA = adjacency_map_->getLinkMapping(res.link_names[0]);
    if (itA != nullptr)
    {
      Eigen::MatrixXd jac;
      Eigen::VectorXd dist_grad;
      Eigen::Isometry3d pose, pose2, pose3;
      jac.resize(6, manip_->numJoints());
      manip_->calcFwdKin(pose, dofvals0, itA->link_name);
      pose = world_to_base_ * pose;

      // For descrete continuous we need to populate cc_transform for plotting to work correctly
      manip_->calcFwdKin(pose3, dofvals1, itA->link_name);
      res.cc_transform[0] = world_to_base_ * pose3;

      manip_->calcJacobian(jac, dofvals0, itA->link_name);
      tesseract_kinematics::jacobianChangeBase(jac, world_to_base_);
      tesseract_kinematics::jacobianChangeRefPoint(jac, pose.linear() * res.nearest_points_local[0]);

      // Eigen::MatrixXd jac_test;
      // jac_test.resize(6, manip_->numJoints());
      // tesseract_kinematics::numericalJacobian(jac_test, world_to_base_, *manip_, dofvals, itA->link_name,
      // res.nearest_points_local[0]); bool check = jac.isApprox(jac_test, 1e-3); assert(check == true);

      dist_grad = -res.normal.transpose() * jac.topRows(3);

      manip_->calcFwdKin(pose2, dofvals0 + dist_grad, itA->link_name);
      pose2 = world_to_base_ * pose2 * itA->transform;
      plotter->plotArrow(
          res.nearest_points[0], pose2 * res.nearest_points_local[0], Eigen::Vector4d(1, 1, 1, 1), 0.005);
    }

    tesseract_environment::AdjacencyMapPair::ConstPtr itB = adjacency_map_->getLinkMapping(res.link_names[1]);
    if (itB != nullptr)
    {
      Eigen::MatrixXd jac;
      Eigen::VectorXd dist_grad;
      Eigen::Isometry3d pose, pose2, pose3;
      jac.resize(6, manip_->numJoints());
      manip_->calcFwdKin(pose, dofvals0, itB->link_name);
      pose = world_to_base_ * pose;

      // For descrete continuous we need to populate cc_transform for plotting to work correctly
      manip_->calcFwdKin(pose3, dofvals1, itB->link_name);
      res.cc_transform[1] = world_to_base_ * pose3;

      // Calculate Jacobian
      manip_->calcJacobian(jac, dofvals0, itB->link_name);
      tesseract_kinematics::jacobianChangeBase(jac, world_to_base_);
      tesseract_kinematics::jacobianChangeRefPoint(jac, pose.linear() * res.nearest_points_local[1]);

      // Eigen::MatrixXd jac_test;
      // jac_test.resize(6, manip_->numJoints());
      // tesseract_kinematics::numericalJacobian(jac_test, world_to_base_, *manip_, dofvals, itB->link_name,
      // res.nearest_points_local[1]); bool check = jac.isApprox(jac_test, 1e-3); assert(check == true)

      dist_grad = res.normal.transpose() * jac.topRows(3);

      manip_->calcFwdKin(pose2, dofvals0 + dist_grad, itB->link_name);
      pose2 = world_to_base_ * pose2 * itB->transform;
      plotter->plotArrow(
          res.nearest_points[1], pose2 * res.nearest_points_local[1], Eigen::Vector4d(1, 1, 1, 1), 0.005);
    }
  }

  plotter->plotContactResults(adjacency_map_->getActiveLinkNames(), dist_results, safety_distance);
}

////////////////////////////////////////

CastCollisionEvaluator::CastCollisionEvaluator(tesseract_kinematics::ForwardKinematics::ConstPtr manip,
                                               tesseract_environment::Environment::ConstPtr env,
                                               tesseract_environment::AdjacencyMap::ConstPtr adjacency_map,
                                               const Eigen::Isometry3d& world_to_base,
                                               SafetyMarginData::ConstPtr safety_margin_data,
                                               tesseract_collision::ContactTestType contact_test_type,
                                               double longest_valid_segment_length,
                                               sco::VarVector vars0,
                                               sco::VarVector vars1,
                                               CollisionExpressionEvaluatorType type,
                                               double safety_margin_buffer)
  : CollisionEvaluator(std::move(manip),
                       std::move(env),
                       std::move(adjacency_map),
                       world_to_base,
                       std::move(safety_margin_data),
                       contact_test_type,
                       longest_valid_segment_length,
                       safety_margin_buffer)
{
  vars0_ = std::move(vars0);
  vars1_ = std::move(vars1);
  evaluator_type_ = type;

  contact_manager_ = env_->getContinuousContactManager();
  contact_manager_->setActiveCollisionObjects(adjacency_map_->getActiveLinkNames());
  contact_manager_->setContactDistanceThreshold(safety_margin_data_->getMaxSafetyMargin() + safety_margin_buffer_);

  switch (evaluator_type_)
  {
    case CollisionExpressionEvaluatorType::START_FREE_END_FREE:
    {
      fn_ = std::bind(
          &CastCollisionEvaluator::CalcDistExpressionsBothFree, this, std::placeholders::_1, std::placeholders::_2);
      break;
    }
    case CollisionExpressionEvaluatorType::START_FIXED_END_FREE:
    {
      fn_ = std::bind(
          &CastCollisionEvaluator::CalcDistExpressionsEndFree, this, std::placeholders::_1, std::placeholders::_2);
      break;
    }
    case CollisionExpressionEvaluatorType::START_FREE_END_FIXED:
    {
      fn_ = std::bind(
          &CastCollisionEvaluator::CalcDistExpressionsStartFree, this, std::placeholders::_1, std::placeholders::_2);
      break;
    }
  };
}

void CastCollisionEvaluator::CalcCollisions(const DblVec& x, tesseract_collision::ContactResultVector& dist_results)
{
  Eigen::VectorXd s0 = sco::getVec(x, vars0_);
  Eigen::VectorXd s1 = sco::getVec(x, vars1_);
  tesseract_collision::ContactResultMap contact_results;

  // The first step is to see if the distance between two states is larger than the longest valid segment. If larger
  // the collision checking is broken up into multiple casted collision checks such that each check is less then
  // the longest valid segment length.
  double dist = (s1 - s0).norm();
  if (dist > longest_valid_segment_length_)
  {
    // Calculate the number state to interpolate
    long cnt = static_cast<long>(std::ceil(dist / longest_valid_segment_length_)) + 1;

    // Create interpolated trajectory between two states that satisfies the longest valid segment length.
    tesseract_common::TrajArray subtraj(cnt, s0.size());
    for (long i = 0; i < s0.size(); ++i)
      subtraj.col(i) = Eigen::VectorXd::LinSpaced(cnt, s0(i), s1(i));

    // Perform casted collision checking for sub trajectory and store results in contacts_vector
    std::vector<tesseract_collision::ContactResultMap> contacts_vector;
    contacts_vector.reserve(static_cast<size_t>(subtraj.rows()));
    bool contact_found = false;
    for (int i = 0; i < subtraj.rows() - 1; ++i)
    {
      tesseract_collision::ContactResultMap contacts;
      tesseract_environment::EnvState::Ptr state0 = state_solver_->getState(manip_->getJointNames(), subtraj.row(i));
      tesseract_environment::EnvState::Ptr state1 =
          state_solver_->getState(manip_->getJointNames(), subtraj.row(i + 1));

      for (const auto& link_name : adjacency_map_->getActiveLinkNames())
        contact_manager_->setCollisionObjectsTransform(
            link_name, state0->transforms[link_name], state1->transforms[link_name]);

      contact_manager_->contactTest(contacts, contact_test_type_);
      if (!contacts.empty())
        contact_found = true;

      contacts_vector.push_back(contacts);
    }

    if (contact_found)
      processInterpolatedCollisionResults(contacts_vector, contact_results);
  }
  else
  {
    tesseract_environment::EnvState::Ptr state0 = state_solver_->getState(manip_->getJointNames(), s0);
    tesseract_environment::EnvState::Ptr state1 = state_solver_->getState(manip_->getJointNames(), s1);
    for (const auto& link_name : adjacency_map_->getActiveLinkNames())
      contact_manager_->setCollisionObjectsTransform(
          link_name, state0->transforms[link_name], state1->transforms[link_name]);

    contact_manager_->contactTest(contact_results, contact_test_type_);

    // Dont include contacts at the fixed state
    for (auto& pair : contact_results)
    {
      const Eigen::Vector2d& data = getSafetyMarginData()->getPairSafetyMarginData(pair.first.first, pair.first.second);
      removeInvalidContactResults(pair.second, data);
    }
  }

  // Flatten results into a single vector
  tesseract_collision::flattenResults(std::move(contact_results), dist_results);
}

void CastCollisionEvaluator::CalcDistExpressions(const DblVec& x, sco::AffExprVector& exprs) { fn_(x, exprs); }

void CastCollisionEvaluator::Plot(const tesseract_visualization::Visualization::Ptr& plotter, const DblVec& x)
{
  tesseract_collision::ContactResultVector dist_results;
  GetCollisionsCached(x, dist_results);
  Eigen::VectorXd dofvals = sco::getVec(x, vars0_);

  Eigen::VectorXd safety_distance(dist_results.size());
  for (auto i = 0u; i < dist_results.size(); ++i)
  {
    const tesseract_collision::ContactResult& res = dist_results[i];
    const Eigen::Vector2d& data = getSafetyMarginData()->getPairSafetyMarginData(res.link_names[0], res.link_names[1]);
    safety_distance[i] = data[0];

    tesseract_environment::AdjacencyMapPair::ConstPtr itA = adjacency_map_->getLinkMapping(res.link_names[0]);
    if (itA != nullptr)
    {
      Eigen::MatrixXd jac;
      Eigen::VectorXd dist_grad;
      Eigen::Isometry3d pose, pose2;
      jac.resize(6, manip_->numJoints());
      manip_->calcFwdKin(pose, dofvals, itA->link_name);
      pose = world_to_base_ * pose;

      manip_->calcJacobian(jac, dofvals, itA->link_name);
      tesseract_kinematics::jacobianChangeBase(jac, world_to_base_);
      tesseract_kinematics::jacobianChangeRefPoint(jac, pose.linear() * res.nearest_points_local[0]);

      // Eigen::MatrixXd jac_test;
      // jac_test.resize(6, manip_->numJoints());
      // tesseract_kinematics::numericalJacobian(jac_test, world_to_base_, *manip_, dofvals, itA->link_name,
      // res.nearest_points_local[0]); bool check = jac.isApprox(jac_test, 1e-3); assert(check == true);

      dist_grad = -res.normal.transpose() * jac.topRows(3);

      manip_->calcFwdKin(pose2, dofvals + dist_grad, itA->link_name);
      pose2 = world_to_base_ * pose2 * itA->transform;
      plotter->plotArrow(
          res.nearest_points[0], pose2 * res.nearest_points_local[0], Eigen::Vector4d(1, 1, 1, 1), 0.005);
    }

    tesseract_environment::AdjacencyMapPair::ConstPtr itB = adjacency_map_->getLinkMapping(res.link_names[1]);
    if (itB != nullptr)
    {
      Eigen::MatrixXd jac;
      Eigen::VectorXd dist_grad;
      Eigen::Isometry3d pose, pose2;
      jac.resize(6, manip_->numJoints());
      manip_->calcFwdKin(pose, dofvals, itB->link_name);
      pose = world_to_base_ * pose;

      // Calculate Jacobian
      manip_->calcJacobian(jac, dofvals, itB->link_name);
      tesseract_kinematics::jacobianChangeBase(jac, world_to_base_);
      tesseract_kinematics::jacobianChangeRefPoint(jac, pose.linear() * res.nearest_points_local[1]);

      // Eigen::MatrixXd jac_test;
      // jac_test.resize(6, manip_->numJoints());
      // tesseract_kinematics::numericalJacobian(jac_test, world_to_base_, *manip_, dofvals, itB->link_name,
      // res.nearest_points_local[1]); bool check = jac.isApprox(jac_test, 1e-3); assert(check == true)

      dist_grad = res.normal.transpose() * jac.topRows(3);

      manip_->calcFwdKin(pose2, dofvals + dist_grad, itB->link_name);
      pose2 = world_to_base_ * pose2 * itB->transform;
      plotter->plotArrow(
          res.nearest_points[1], pose2 * res.nearest_points_local[1], Eigen::Vector4d(1, 1, 1, 1), 0.005);
    }
  }

  plotter->plotContactResults(adjacency_map_->getActiveLinkNames(), dist_results, safety_distance);
}

//////////////////////////////////////////

CollisionCost::CollisionCost(tesseract_kinematics::ForwardKinematics::ConstPtr manip,
                             tesseract_environment::Environment::ConstPtr env,
                             tesseract_environment::AdjacencyMap::ConstPtr adjacency_map,
                             const Eigen::Isometry3d& world_to_base,
                             SafetyMarginData::ConstPtr safety_margin_data,
                             tesseract_collision::ContactTestType contact_test_type,
                             sco::VarVector vars,
                             double safety_margin_buffer)
  : Cost("collision")
  , m_calc(new SingleTimestepCollisionEvaluator(std::move(manip),
                                                std::move(env),
                                                std::move(adjacency_map),
                                                world_to_base,
                                                std::move(safety_margin_data),
                                                contact_test_type,
                                                std::move(vars),
                                                safety_margin_buffer))
{
}

CollisionCost::CollisionCost(tesseract_kinematics::ForwardKinematics::ConstPtr manip,
                             tesseract_environment::Environment::ConstPtr env,
                             tesseract_environment::AdjacencyMap::ConstPtr adjacency_map,
                             const Eigen::Isometry3d& world_to_base,
                             SafetyMarginData::ConstPtr safety_margin_data,
                             tesseract_collision::ContactTestType contact_test_type,
                             double longest_valid_segment_length,
                             sco::VarVector vars0,
                             sco::VarVector vars1,
                             CollisionExpressionEvaluatorType type,
                             bool discrete,
                             double safety_margin_buffer)
{
  if (discrete)
  {
    name_ = "discrete_continuous_collision";
    m_calc = std::make_shared<DiscreteCollisionEvaluator>(std::move(manip),
                                                          std::move(env),
                                                          std::move(adjacency_map),
                                                          world_to_base,
                                                          std::move(safety_margin_data),
                                                          contact_test_type,
                                                          longest_valid_segment_length,
                                                          std::move(vars0),
                                                          std::move(vars1),
                                                          type,
                                                          safety_margin_buffer);
  }
  else
  {
    name_ = "cast_continuous_collision";
    m_calc = std::make_shared<CastCollisionEvaluator>(std::move(manip),
                                                      std::move(env),
                                                      std::move(adjacency_map),
                                                      world_to_base,
                                                      std::move(safety_margin_data),
                                                      contact_test_type,
                                                      longest_valid_segment_length,
                                                      std::move(vars0),
                                                      std::move(vars1),
                                                      type,
                                                      safety_margin_buffer);
  }
}

sco::ConvexObjective::Ptr CollisionCost::convex(const sco::DblVec& x, sco::Model* model)
{
  sco::ConvexObjective::Ptr out(new sco::ConvexObjective(model));
  sco::AffExprVector exprs;
  m_calc->CalcDistExpressions(x, exprs);

  tesseract_collision::ContactResultVector dist_results;
  m_calc->GetCollisionsCached(x, dist_results);
  for (std::size_t i = 0; i < exprs.size(); ++i)
  {
    const Eigen::Vector2d& data = m_calc->getSafetyMarginData()->getPairSafetyMarginData(dist_results[i].link_names[0],
                                                                                         dist_results[i].link_names[1]);

    sco::AffExpr viol = sco::exprSub(sco::AffExpr(data[0]), exprs[i]);
    out->addHinge(viol, data[1]);
  }
  return out;
}

double CollisionCost::value(const sco::DblVec& x)
{
  DblVec dists;
  m_calc->CalcDists(x, dists);

  tesseract_collision::ContactResultVector dist_results;
  m_calc->GetCollisionsCached(x, dist_results);
  double out = 0;
  for (std::size_t i = 0; i < dists.size(); ++i)
  {
    const Eigen::Vector2d& data = m_calc->getSafetyMarginData()->getPairSafetyMarginData(dist_results[i].link_names[0],
                                                                                         dist_results[i].link_names[1]);
    out += sco::pospart(data[0] - dists[i]) * data[1];
  }
  return out;
}

void CollisionCost::Plot(const tesseract_visualization::Visualization::Ptr& plotter, const DblVec& x)
{
  m_calc->Plot(plotter, x);
}
// ALMOST EXACTLY COPIED FROM CollisionCost

CollisionConstraint::CollisionConstraint(tesseract_kinematics::ForwardKinematics::ConstPtr manip,
                                         tesseract_environment::Environment::ConstPtr env,
                                         tesseract_environment::AdjacencyMap::ConstPtr adjacency_map,
                                         const Eigen::Isometry3d& world_to_base,
                                         SafetyMarginData::ConstPtr safety_margin_data,
                                         tesseract_collision::ContactTestType contact_test_type,
                                         sco::VarVector vars,
                                         double safety_margin_buffer)
  : m_calc(new SingleTimestepCollisionEvaluator(std::move(manip),
                                                std::move(env),
                                                std::move(adjacency_map),
                                                world_to_base,
                                                std::move(safety_margin_data),
                                                contact_test_type,
                                                std::move(vars),
                                                safety_margin_buffer))
{
  name_ = "collision";
}

CollisionConstraint::CollisionConstraint(tesseract_kinematics::ForwardKinematics::ConstPtr manip,
                                         tesseract_environment::Environment::ConstPtr env,
                                         tesseract_environment::AdjacencyMap::ConstPtr adjacency_map,
                                         const Eigen::Isometry3d& world_to_base,
                                         SafetyMarginData::ConstPtr safety_margin_data,
                                         tesseract_collision::ContactTestType contact_test_type,
                                         double longest_valid_segment_length,
                                         sco::VarVector vars0,
                                         sco::VarVector vars1,
                                         CollisionExpressionEvaluatorType type,
                                         bool discrete,
                                         double safety_margin_buffer)
{
  if (discrete)
  {
    name_ = "discrete_continuous_collision";
    m_calc = std::make_shared<DiscreteCollisionEvaluator>(std::move(manip),
                                                          std::move(env),
                                                          std::move(adjacency_map),
                                                          world_to_base,
                                                          std::move(safety_margin_data),
                                                          contact_test_type,
                                                          longest_valid_segment_length,
                                                          std::move(vars0),
                                                          std::move(vars1),
                                                          type,
                                                          safety_margin_buffer);
  }
  else
  {
    name_ = "cast_continuous_collision";
    m_calc = std::make_shared<CastCollisionEvaluator>(std::move(manip),
                                                      std::move(env),
                                                      std::move(adjacency_map),
                                                      world_to_base,
                                                      std::move(safety_margin_data),
                                                      contact_test_type,
                                                      longest_valid_segment_length,
                                                      std::move(vars0),
                                                      std::move(vars1),
                                                      type,
                                                      safety_margin_buffer);
  }
}

sco::ConvexConstraints::Ptr CollisionConstraint::convex(const sco::DblVec& x, sco::Model* model)
{
  sco::ConvexConstraints::Ptr out(new sco::ConvexConstraints(model));
  sco::AffExprVector exprs;
  m_calc->CalcDistExpressions(x, exprs);

  tesseract_collision::ContactResultVector dist_results;
  m_calc->GetCollisionsCached(x, dist_results);
  for (std::size_t i = 0; i < exprs.size(); ++i)
  {
    const Eigen::Vector2d& data = m_calc->getSafetyMarginData()->getPairSafetyMarginData(dist_results[i].link_names[0],
                                                                                         dist_results[i].link_names[1]);

    sco::AffExpr viol = sco::exprSub(sco::AffExpr(data[0]), exprs[i]);
    out->addIneqCnt(sco::exprMult(viol, data[1]));
  }
  return out;
}

DblVec CollisionConstraint::value(const sco::DblVec& x)
{
  DblVec dists;
  m_calc->CalcDists(x, dists);

  tesseract_collision::ContactResultVector dist_results;
  m_calc->GetCollisionsCached(x, dist_results);
  DblVec out(dists.size());
  for (std::size_t i = 0; i < dists.size(); ++i)
  {
    const Eigen::Vector2d& data = m_calc->getSafetyMarginData()->getPairSafetyMarginData(dist_results[i].link_names[0],
                                                                                         dist_results[i].link_names[1]);

    out[i] = sco::pospart(data[0] - dists[i]) * data[1];
  }
  return out;
}
}  // namespace trajopt
