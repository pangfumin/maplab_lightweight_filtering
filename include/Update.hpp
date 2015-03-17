/*
 * Update.hpp
 *
 *  Created on: Feb 9, 2014
 *      Author: Bloeschm
 */

#ifndef UPDATEMODEL_HPP_
#define UPDATEMODEL_HPP_

#include <Eigen/Dense>
#include <iostream>
#include "kindr/rotations/RotationEigen.hpp"
#include "ModelBase.hpp"
#include "State.hpp"
#include "PropertyHandler.hpp"
#include "SigmaPoints.hpp"
#include "Prediction.hpp"
#include "OutlierDetection.hpp"
#include <type_traits>

namespace LWF{

template<typename Innovation, typename FilterState, typename Meas, typename Noise, typename OutlierDetection = OutlierDetectionDefault, bool isCoupled = false>
class Update: public ModelBase<typename FilterState::mtState,Innovation,Meas,Noise>, public PropertyHandler{
 public:
  static_assert(!isCoupled || Noise::D_ == FilterState::noiseExtensionDim_,"Noise Size for coupled Update must match noise extension of prediction!");
  typedef FilterState mtFilterState;
  typedef typename mtFilterState::mtState mtState;
  typedef typename mtFilterState::mtFilterCovMat mtFilterCovMat;
  typedef typename mtFilterState::mtPredictionMeas mtPredictionMeas;
  typedef typename mtFilterState::mtPredictionNoise mtPredictionNoise;
  typedef Innovation mtInnovation;
  typedef Meas mtMeas;
  typedef Noise mtNoise;
  typedef OutlierDetection mtOutlierDetection;
  static const bool coupledToPrediction_ = isCoupled;
  bool useSpecialLinearizationPoint_;
  typedef ModelBase<mtState,mtInnovation,mtMeas,mtNoise> mtModelBase;
  typename mtModelBase::mtJacInput H_;
  typename mtModelBase::mtJacNoise Hn_;
  typename mtNoise::mtCovMat updnoiP_;
  Eigen::Matrix<double,mtPredictionNoise::D_,mtNoise::D_> preupdnoiP_;
  Eigen::Matrix<double,mtNoise::D_,mtNoise::D_> noiP_;
  Eigen::Matrix<double,mtState::D_,mtInnovation::D_> C_;
  mtInnovation y_;
  typename mtInnovation::mtCovMat Py_;
  typename mtInnovation::mtCovMat Pyinv_;
  typename mtInnovation::mtDifVec innVector_;
  mtInnovation yIdentity_;
  typename mtState::mtDifVec updateVec_;
  mtState linState_;
  double updateVecNorm_;
  Eigen::Matrix<double,mtState::D_,mtInnovation::D_> K_;
  Eigen::Matrix<double,mtInnovation::D_,mtState::D_> Pyx_;

  SigmaPoints<mtState,2*mtState::D_+1,2*(mtState::D_+mtNoise::D_)+1,0> stateSigmaPoints_;
  SigmaPoints<mtNoise,2*mtNoise::D_+1,2*(mtState::D_+mtNoise::D_)+1,2*mtState::D_> stateSigmaPointsNoi_;
  SigmaPoints<mtInnovation,2*(mtState::D_+mtNoise::D_)+1,2*(mtState::D_+mtNoise::D_)+1,0> innSigmaPoints_;
  SigmaPoints<mtNoise,2*(mtNoise::D_+mtPredictionNoise::D_)+1,2*(mtState::D_+mtNoise::D_+mtPredictionNoise::D_)+1,2*(mtState::D_)> coupledStateSigmaPointsNoi_;
  SigmaPoints<mtInnovation,2*(mtState::D_+mtNoise::D_+mtPredictionNoise::D_)+1,2*(mtState::D_+mtNoise::D_+mtPredictionNoise::D_)+1,0> coupledInnSigmaPoints_;
  SigmaPoints<LWF::VectorElement<mtState::D_>,2*mtState::D_+1,2*mtState::D_+1,0> updateVecSP_;
  SigmaPoints<mtState,2*mtState::D_+1,2*mtState::D_+1,0> posterior_;
  double alpha_;
  double beta_;
  double kappa_;
  double updateVecNormTermination_;
  int maxNumIteration_;
  mtOutlierDetection outlierDetection_;
  Update(){
    alpha_ = 1e-3;
    beta_ = 2.0;
    kappa_ = 0.0;
    updateVecNormTermination_ = 1e-6;
    maxNumIteration_  = 10;
    updnoiP_ = mtNoise::mtCovMat::Identity()*0.0001;
    noiP_.setZero();
    preupdnoiP_ = Eigen::Matrix<double,mtPredictionNoise::D_,mtNoise::D_>::Zero();
    useSpecialLinearizationPoint_ = false;
    yIdentity_.setIdentity();
    updateVec_.setIdentity();
    refreshNoiseSigmaPoints();
    refreshUKFParameter();
    mtNoise n;
    n.registerCovarianceToPropertyHandler_(updnoiP_,this,"UpdateNoise.");
    doubleRegister_.registerScalar("alpha",alpha_);
    doubleRegister_.registerScalar("beta",beta_);
    doubleRegister_.registerScalar("kappa",kappa_);
    doubleRegister_.registerScalar("updateVecNormTermination",updateVecNormTermination_);
    intRegister_.registerScalar("maxNumIteration",maxNumIteration_);
    outlierDetection_.setEnabledAll(false);
  };
  void refreshNoiseSigmaPoints(){
    if(noiP_ != updnoiP_){
      noiP_ = updnoiP_;
      stateSigmaPointsNoi_.computeFromZeroMeanGaussian(noiP_);
    }
  }
  void refreshUKFParameter(){
    stateSigmaPoints_.computeParameter(alpha_,beta_,kappa_);
    innSigmaPoints_.computeParameter(alpha_,beta_,kappa_);
    coupledInnSigmaPoints_.computeParameter(alpha_,beta_,kappa_);
    updateVecSP_.computeParameter(alpha_,beta_,kappa_);
    posterior_.computeParameter(alpha_,beta_,kappa_);
    stateSigmaPointsNoi_.computeParameter(alpha_,beta_,kappa_);
    stateSigmaPointsNoi_.computeFromZeroMeanGaussian(noiP_);
    coupledStateSigmaPointsNoi_.computeParameter(alpha_,beta_,kappa_);
  }
  void refreshProperties(){
    refreshPropertiesCustom();
    refreshUKFParameter();
  }
  virtual void refreshPropertiesCustom(){}
  virtual void preProcess(mtFilterState& filterState, const mtMeas& meas){};
  virtual void postProcess(mtFilterState& filterState, const mtMeas& meas, mtOutlierDetection outlierDetection){};
  virtual ~Update(){};
  int performUpdate(mtFilterState& filterState, const mtMeas& meas){
    switch(filterState.mode_){
      case ModeEKF:
        return performUpdateEKF(filterState,meas);
      case ModeUKF:
        return performUpdateUKF(filterState,meas);
      default:
        return performUpdateEKF(filterState,meas);
    }
  }
  int performUpdateEKF(mtFilterState& filterState, const mtMeas& meas){
    preProcess(filterState,meas);
    if(!useSpecialLinearizationPoint_){
      this->jacInput(H_,filterState.state_,meas);
      this->jacNoise(Hn_,filterState.state_,meas);
      this->eval(y_,filterState.state_,meas);
    } else {
      filterState.state_.boxPlus(filterState.difVecLin_,linState_);
      this->jacInput(H_,linState_,meas);
      this->jacNoise(Hn_,linState_,meas);
      this->eval(y_,linState_,meas);
    }

    if(isCoupled){
      C_ = filterState.G_*preupdnoiP_*Hn_.transpose();
      Py_ = H_*filterState.cov_*H_.transpose() + Hn_*updnoiP_*Hn_.transpose() + H_*C_ + C_.transpose()*H_.transpose();
    } else {
      Py_ = H_*filterState.cov_*H_.transpose() + Hn_*updnoiP_*Hn_.transpose();
    }
    y_.boxMinus(yIdentity_,innVector_);

    // Outlier detection // TODO: adapt for special linearization point
    outlierDetection_.doOutlierDetection(innVector_,Py_,H_);
    Pyinv_.setIdentity();
    Py_.llt().solveInPlace(Pyinv_);

    // Kalman Update
    if(isCoupled){
      K_ = (filterState.cov_*H_.transpose()+C_)*Pyinv_;
    } else {
      K_ = filterState.cov_*H_.transpose()*Pyinv_;
    }
    filterState.cov_ = filterState.cov_ - K_*Py_*K_.transpose();
    if(!useSpecialLinearizationPoint_){
      updateVec_ = -K_*innVector_;
    } else {
      updateVec_ = -K_*(innVector_-H_*filterState.difVecLin_); // includes correction for offseted linearization point
    }
    filterState.state_.boxPlus(updateVec_,filterState.state_);
    postProcess(filterState,meas,outlierDetection_);
    return 0;
  }
  int performUpdateIEKF(mtFilterState& filterState, const mtMeas& meas){
    preProcess(filterState,meas);
    mtState linState = filterState.state_;
    updateVecNorm_ = updateVecNormTermination_;
    for(unsigned int i=0;i<maxNumIteration_ & updateVecNorm_>=updateVecNormTermination_;i++){
      this->jacInput(H_,linState,meas);
      this->jacNoise(Hn_,linState,meas);
      this->eval(y_,linState,meas);

      if(isCoupled){
        C_ = filterState.G_*preupdnoiP_*Hn_.transpose();
        Py_ = H_*filterState.cov_*H_.transpose() + Hn_*updnoiP_*Hn_.transpose() + H_*C_ + C_.transpose()*H_.transpose();
      } else {
        Py_ = H_*filterState.cov_*H_.transpose() + Hn_*updnoiP_*Hn_.transpose();
      }
      y_.boxMinus(yIdentity_,innVector_);

      // Outlier detection
      outlierDetection_.doOutlierDetection(innVector_,Py_,H_);
      Pyinv_.setIdentity();
      Py_.llt().solveInPlace(Pyinv_);

      // Kalman Update
      if(isCoupled){
        K_ = (filterState.cov_*H_.transpose()+C_)*Pyinv_;
      } else {
        K_ = filterState.cov_*H_.transpose()*Pyinv_;
      }
      linState.boxMinus(filterState.state_,filterState.difVecLin_);
      updateVec_ = -K_*(innVector_-H_*filterState.difVecLin_); // includes correction for offseted linearization point
      filterState.state_.boxPlus(updateVec_,linState);
      updateVecNorm_ = updateVec_.norm();
    }
    filterState.state_ = linState;
    filterState.cov_ = filterState.cov_ - K_*Py_*K_.transpose();
    postProcess(filterState,meas,outlierDetection_);
    return 0;
  }
  int performUpdateUKF(mtFilterState& filterState, const mtMeas& meas){
    preProcess(filterState,meas);
    handleUpdateSigmaPoints<isCoupled>(filterState,meas);
    y_.boxMinus(yIdentity_,innVector_);

    outlierDetection_.doOutlierDetection(innVector_,Py_,Pyx_);
    Pyinv_.setIdentity();
    Py_.llt().solveInPlace(Pyinv_);

    // Kalman Update
    K_ = Pyx_.transpose()*Pyinv_;
    filterState.cov_ = filterState.cov_ - K_*Py_*K_.transpose();
    updateVec_ = -K_*innVector_;

    // Adapt for proper linearization point
    updateVecSP_.computeFromZeroMeanGaussian(filterState.cov_);
    for(unsigned int i=0;i<2*mtState::D_+1;i++){
      filterState.state_.boxPlus(updateVec_+updateVecSP_(i).v_,posterior_(i));
    }
    filterState.state_ = posterior_.getMean();
    filterState.cov_ = posterior_.getCovarianceMatrix(filterState.state_);
    postProcess(filterState,meas,outlierDetection_);
    return 0;
  }
  template<bool IC = isCoupled, typename std::enable_if<(IC)>::type* = nullptr>
  void handleUpdateSigmaPoints(mtFilterState& filterState, const mtMeas& meas){
    coupledStateSigmaPointsNoi_.extendZeroMeanGaussian(filterState.stateSigmaPointsNoi_,updnoiP_,preupdnoiP_);
    for(unsigned int i=0;i<coupledInnSigmaPoints_.L_;i++){
      this->eval(coupledInnSigmaPoints_(i),filterState.stateSigmaPointsPre_(i),meas,coupledStateSigmaPointsNoi_(i));
    }
    y_ = coupledInnSigmaPoints_.getMean();
    Py_ = coupledInnSigmaPoints_.getCovarianceMatrix(y_);
    Pyx_ = (coupledInnSigmaPoints_.getCovarianceMatrix(filterState.stateSigmaPointsPre_));
  }
  template<bool IC = isCoupled, typename std::enable_if<(!IC)>::type* = nullptr>
  void handleUpdateSigmaPoints(mtFilterState& filterState, const mtMeas& meas){
    refreshNoiseSigmaPoints();
    stateSigmaPoints_.computeFromGaussian(filterState.state_,filterState.cov_);
    for(unsigned int i=0;i<innSigmaPoints_.L_;i++){
      this->eval(innSigmaPoints_(i),stateSigmaPoints_(i),meas,stateSigmaPointsNoi_(i));
    }
    y_ = innSigmaPoints_.getMean();
    Py_ = innSigmaPoints_.getCovarianceMatrix(y_);
    Pyx_ = (innSigmaPoints_.getCovarianceMatrix(stateSigmaPoints_));
  }
};

}

#endif /* UPDATEMODEL_HPP_ */
