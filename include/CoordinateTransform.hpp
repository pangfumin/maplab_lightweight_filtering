/*
 * CoordinateTransform.hpp
 *
 *  Created on: Feb 9, 2014
 *      Author: Bloeschm
 */

#ifndef CoordinateTransform_HPP_
#define CoordinateTransform_HPP_

#include <Eigen/Dense>
#include "ModelBase.hpp"
#include "State.hpp"

namespace LWF{

template<typename Input, typename Output, bool useDynamicMatrix = false>
class CoordinateTransform: public ModelBase<Input,Output,Input,Input,useDynamicMatrix>{
 public:
  typedef ModelBase<Input,Output,Input,Input,useDynamicMatrix> Base;
  using Base::eval;
  using Base::jacInput;
  typedef Input mtInput;
  typedef LWFMatrix<mtInput::D_,mtInput::D_,useDynamicMatrix> mtInputCovMat;
  typedef Output mtOutput;
  typedef LWFMatrix<mtOutput::D_,mtOutput::D_,useDynamicMatrix> mtOutputCovMat;
  typedef typename Base::mtJacInput mtJacInput;
  mtJacInput J_;
  mtOutputCovMat outputCov_;
  CoordinateTransform(){};
  virtual ~CoordinateTransform(){};
  mtOutput transformState(mtInput& input) const{
    mtOutput output;
    eval(output, input, input);
    return output;
  }
  mtOutputCovMat transformCovMat(const mtInput& input,const mtInputCovMat& inputCov){
    J_ = jacInput(input,input);
    outputCov_ = J_*inputCov*J_.transpose();
    postProcess(outputCov_,input);
    return outputCov_;
  }
  virtual void postProcess(mtOutputCovMat& cov,const mtInput& input){}
};

}

#endif /* CoordinateTransform_HPP_ */
