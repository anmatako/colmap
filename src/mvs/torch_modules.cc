// Copyright (c) 2021, Microsoft
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of ETH Zurich and UNC Chapel Hill nor the names of
//       its contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: Antonios Matakos (anmatako-at-microsoft-dot-com)

#include "mvs/torch_modules.h"
#include <iostream>

namespace colmap {
namespace mvs {

static const int kNumStages = 4;
static const torch::nn::functional::InterpolateFuncOptions kInterpBilinear =
    torch::nn::functional::InterpolateFuncOptions()
        .scale_factor(std::vector<double>(2, 2.0))
        .mode(torch::kBilinear)
        .align_corners(false);
static const torch::nn::functional::GridSampleFuncOptions kGridSampleBorder =
    torch::nn::functional::GridSampleFuncOptions()
        .padding_mode(torch::kBorder)
        .mode(torch::kBilinear)
        .align_corners(false);
static const torch::nn::functional::GridSampleFuncOptions kGridSampleZeros =
    torch::nn::functional::GridSampleFuncOptions()
        .padding_mode(torch::kZeros)
        .mode(torch::kBilinear)
        .align_corners(false);

ConvBnReLU1DImpl::ConvBnReLU1DImpl(int64_t in_channels, int64_t out_channels,
                                   int64_t kernel_size, int64_t stride,
                                   int64_t padding, int64_t dilation)
    : Module("ConvBnReLU1D") {
  conv = register_module(
      "conv", torch::nn::Conv1d(torch::nn::Conv1dOptions(
                                    in_channels, out_channels, kernel_size)
                                    .stride(stride)
                                    .padding(padding)
                                    .dilation(dilation)
                                    .bias(false)));
  norm = register_module(
      "bn",
      torch::nn::BatchNorm1d(torch::nn::BatchNorm1dOptions(out_channels)));
}

torch::Tensor ConvBnReLU1DImpl::forward(const torch::Tensor& input) {
  return torch::nn::functional::relu(
      norm->forward(conv->forward(input)),
      torch::nn::functional::ReLUFuncOptions(true));
}

ConvBnReLU2DImpl::ConvBnReLU2DImpl(int64_t in_channels, int64_t out_channels,
                                   int64_t kernel_size, int64_t stride,
                                   int64_t padding, int64_t dilation)
    : Module("ConvBnReLU2D") {
  conv = register_module(
      "conv", torch::nn::Conv2d(torch::nn::Conv2dOptions(
                                    in_channels, out_channels, kernel_size)
                                    .stride(stride)
                                    .padding(padding)
                                    .dilation(dilation)
                                    .bias(false)));
  norm = register_module(
      "bn",
      torch::nn::BatchNorm2d(torch::nn::BatchNorm2dOptions(out_channels)));
}

torch::Tensor ConvBnReLU2DImpl::forward(const torch::Tensor& input) {
  return torch::nn::functional::relu(
      norm->forward(conv->forward(input)),
      torch::nn::functional::ReLUFuncOptions(true));
}

ConvBnReLU3DImpl::ConvBnReLU3DImpl(int64_t in_channels, int64_t out_channels,
                                   int64_t kernel_size, int64_t stride,
                                   int64_t padding, int64_t dilation)
    : Module("ConvBnReLU3D") {
  conv = register_module(
      "conv", torch::nn::Conv3d(torch::nn::Conv3dOptions(
                                    in_channels, out_channels, kernel_size)
                                    .stride(stride)
                                    .padding(padding)
                                    .dilation(dilation)
                                    .bias(false)));
  norm = register_module(
      "bn",
      torch::nn::BatchNorm3d(torch::nn::BatchNorm3dOptions(out_channels)));
}

torch::Tensor ConvBnReLU3DImpl::forward(const torch::Tensor& input) {
  return torch::nn::functional::relu(
      norm->forward(conv->forward(input)),
      torch::nn::functional::ReLUFuncOptions(true));
}

RefinementImpl::RefinementImpl() : Module("Refinement") {
  conv = register_module("conv", ConvBnReLU2D(3, 8));
  deconv = register_module(
      "deconv",
      torch::nn::Sequential(
          ConvBnReLU2D(1, 8), ConvBnReLU2D(8, 8),
          torch::nn::ConvTranspose2d(torch::nn::ConvTranspose2dOptions(8, 8, 3)
                                         .padding(1)
                                         .output_padding(1)
                                         .stride(2)
                                         .bias(false)),
          torch::nn::BatchNorm2d(torch::nn::BatchNorm2dOptions(8))));
  residual = register_module(
      "residual",
      torch::nn::Sequential(
          ConvBnReLU2D(16, 8),
          torch::nn::Conv2d(
              torch::nn::Conv2dOptions(8, 1, 3).padding(1).bias(false))));
}

torch::Tensor RefinementImpl::forward(const torch::Tensor& image,
                                      const torch::Tensor& depth_init,
                                      const double depth_min,
                                      const double depth_max) {
  torch::Tensor depth = (depth_init - depth_min) / (depth_max - depth_min);

  torch::Tensor image_conv = conv->forward(image);
  torch::Tensor depth_deconv = torch::nn::functional::relu(
      deconv->forward(depth), torch::nn::functional::ReLUFuncOptions(true));
  torch::Tensor concat = torch::cat({depth_deconv, image_conv}, 1);

  depth = torch::nn::functional::interpolate(depth, kInterpBilinear) +
          residual->forward(concat);
  return (depth * (depth_max - depth_min) + depth_min).squeeze(1);
}

FeatureNetImpl::FeatureNetImpl() : Module("FeatureNet") {
  stage1 = register_module(
      "stage1", torch::nn::Sequential(
                    ConvBnReLU2D(3, 8, 3, 1, 1), ConvBnReLU2D(8, 8, 3, 1, 1),
                    ConvBnReLU2D(8, 16, 5, 2, 2), ConvBnReLU2D(16, 16, 3, 1, 1),
                    ConvBnReLU2D(16, 16, 3, 1, 1)));
  stage2 = register_module(
      "stage2", torch::nn::Sequential(ConvBnReLU2D(16, 32, 5, 2, 2),
                                      ConvBnReLU2D(32, 32, 3, 1, 1),
                                      ConvBnReLU2D(32, 32, 3, 1, 1)));
  stage3 = register_module(
      "stage3", torch::nn::Sequential(ConvBnReLU2D(32, 64, 5, 2, 2),
                                      ConvBnReLU2D(64, 64, 3, 1, 1),
                                      ConvBnReLU2D(64, 64, 3, 1, 1)));
  output1 = register_module(
      "output1",
      torch::nn::Conv2d(torch::nn::Conv2dOptions(64, 16, 1).bias(false)));
  output2 = register_module(
      "output2",
      torch::nn::Conv2d(torch::nn::Conv2dOptions(64, 32, 1).bias(false)));
  output3 = register_module(
      "output3",
      torch::nn::Conv2d(torch::nn::Conv2dOptions(64, 64, 1).bias(false)));
  inner1 = register_module(
      "inner1",
      torch::nn::Conv2d(torch::nn::Conv2dOptions(16, 64, 1).bias(true)));
  inner2 = register_module(
      "inner2",
      torch::nn::Conv2d(torch::nn::Conv2dOptions(32, 64, 1).bias(true)));
}

std::vector<torch::Tensor> FeatureNetImpl::forward(const torch::Tensor& input) {
  std::vector<torch::Tensor> output(kNumStages);
  torch::Tensor res1 = stage1->forward(input);
  torch::Tensor res2 = stage2->forward(res1);
  torch::Tensor res3 = stage3->forward(res2);
  output[3] = output3->forward(res3);

  const std::vector<double> upscale_factors(2, 2.0);
  torch::Tensor intra_feat2 =
      torch::nn::functional::interpolate(res3, kInterpBilinear) +
      inner2->forward(res2);
  output[2] = output2->forward(intra_feat2);

  torch::Tensor intra_feat1 =
      torch::nn::functional::interpolate(intra_feat2, kInterpBilinear) +
      inner1->forward(res1);
  output[1] = output1->forward(intra_feat1);
  return output;
}

FeatureWeightNetImpl::FeatureWeightNetImpl(const int num_neighbors,
                                           const int num_groups)
    : Module("FeatureWeightNet"),
      num_neighbors(num_neighbors),
      num_groups(num_groups) {
  feature_weight = register_module(
      "feature_weight",
      torch::nn::Sequential(
          ConvBnReLU3D(num_groups, 16, 1, 1, 0), ConvBnReLU3D(16, 8, 1, 1, 0),
          torch::nn::Conv3d(
              torch::nn::Conv3dOptions(8, 1, 1).stride(1).padding(0)),
          torch::nn::Sigmoid()));
}

torch::Tensor FeatureWeightNetImpl::forward(const torch::Tensor& feature,
                                            const torch::Tensor& grid) {
  const int64_t batch_size = feature.size(0);
  const int64_t num_channels = feature.size(1);
  const int64_t height = feature.size(2);
  const int64_t width = feature.size(3);

  torch::Tensor weight =
      torch::nn::functional::grid_sample(feature, grid, kGridSampleBorder)
          .view({batch_size, num_groups, num_channels / num_groups,
                 num_neighbors, height, width});
  weight = (weight * feature
                         .view({{batch_size, num_groups,
                                 num_channels / num_groups, height, width}})
                         .unsqueeze(3))
               .mean(2);
  return feature_weight->forward(weight).squeeze(1);
}

SimilarityNetImpl::SimilarityNetImpl(const int num_groups)
    : Module("SimilarityNet") {
  conv = register_module(
      "conv",
      torch::nn::Sequential(
          ConvBnReLU3D(num_groups, 16, 1, 1, 0), ConvBnReLU3D(16, 8, 1, 1, 0),
          torch::nn::Conv3d(
              torch::nn::Conv3dOptions(8, 1, 1).stride(1).padding(0))));
}

torch::Tensor SimilarityNetImpl::forward(const torch::Tensor& similarity,
                                         const torch::Tensor& grid,
                                         const torch::Tensor& weight) {
  const int64_t batch_size = similarity.size(0);
  const int64_t num_depth = similarity.size(2);
  const int64_t height = similarity.size(3);
  const int64_t width = similarity.size(4);
  const int64_t num_neighbors = grid.size(1) / height;

  torch::Tensor score =
      torch::nn::functional::grid_sample(conv->forward(similarity).squeeze(1),
                                         grid, kGridSampleBorder)
          .view({batch_size, num_depth, num_neighbors, height, width});
  return torch::sum(score * weight, 2);
}

PixelwiseNetImpl::PixelwiseNetImpl(const int num_groups)
    : Module("PixelwiseNet") {
  conv = register_module(
      "conv",
      torch::nn::Sequential(
          ConvBnReLU3D(num_groups, 16, 1, 1, 0), ConvBnReLU3D(16, 8, 1, 1, 0),
          torch::nn::Conv3d(
              torch::nn::Conv3dOptions(8, 1, 1).stride(1).padding(0)),
          torch::nn::Sigmoid()));
}

torch::Tensor PixelwiseNetImpl::forward(const torch::Tensor& input) {
  return std::get<0>(torch::max(conv->forward(input).squeeze(1), 1, true));
}

InitDepthImpl::InitDepthImpl(const int num_samples, const double interval_scale)
    : Module("InitDepth"),
      num_samples(num_samples),
      interval_scale(interval_scale) {}

torch::Tensor InitDepthImpl::forward(const torch::Tensor& depth_init,
                                     const double depth_min,
                                     const double depth_max,
                                     const int64_t batch_size,
                                     const int64_t height, const int64_t width,
                                     torch::Device device) {
  const double inv_depth_min = 1.0 / depth_min;
  const double inv_depth_max = 1.0 / depth_max;
  torch::Tensor depth;
  if (!depth_init.defined()) {
    const int rand_num_samples = 48;
    depth = torch::rand({batch_size, rand_num_samples, height, width},
                        torch::device(device)) +
            torch::arange(0, rand_num_samples, 1, torch::device(device))
                .view({1, rand_num_samples, 1, 1});
    return 1.0 / (((inv_depth_min - inv_depth_max) / rand_num_samples) * depth +
                  inv_depth_max);
  } else if (num_samples == 1) {
    return depth_init.detach();
  } else {
    depth = torch::arange(-num_samples / 2, num_samples / 2, 1,
                          torch::device(device))
                .view({1, num_samples, 1, 1})
                .repeat({batch_size, 1, height, width});
    depth = 1.0 / depth_init.detach() +
            (inv_depth_min - inv_depth_max) * interval_scale * depth;
    return 1.0 / depth.clamp(inv_depth_max, inv_depth_min);
  }
}

PropagationImpl::PropagationImpl() : Module("Propagation") {}

torch::Tensor PropagationImpl::forward(const torch::Tensor& depth,
                                       const torch::Tensor& grid,
                                       const double depth_min,
                                       const double depth_max) {
  const int64_t batch_size = depth.size(0);
  const int64_t num_depth = depth.size(1);
  const int64_t height = depth.size(2);
  const int64_t width = depth.size(3);
  const int64_t num_neighbors = grid.size(1) / height;

  torch::Tensor prop_depth =
      torch::nn::functional::grid_sample(
          depth.select(1, num_depth / 2).unsqueeze(1), grid, kGridSampleBorder)
          .view({batch_size, num_neighbors, height, width});
  return std::get<0>(torch::sort(torch::cat({depth, prop_depth}, 1), 1));
}

EvaluationImpl::EvaluationImpl(const int num_groups, const int stage)
    : Module("Evaluation"), num_groups(num_groups) {
  if (stage == 3) {
    pixelwise_net = register_module("pixelwise_net", PixelwiseNet(num_groups));
  }
  similarity_net = register_module("similarity_net", SimilarityNet(num_groups));
}

std::tuple<torch::Tensor, torch::Tensor, torch::Tensor> EvaluationImpl::forward(
    const torch::Tensor& ref_feature_in, const torch::TensorList& src_features,
    const torch::Tensor& ref_proj_mtx, const torch::TensorList& src_proj_mtx,
    const torch::Tensor& depth_init, const torch::Tensor& grid,
    const torch::Tensor& weight, const torch::Tensor& view_weights,
    const bool is_inverse) {
  torch::Tensor depth = depth_init;
  torch::Device device = ref_feature_in.device();
  const int64_t batch_size = ref_feature_in.size(0);
  const int64_t num_channels = ref_feature_in.size(1);
  const int64_t height = ref_feature_in.size(2);
  const int64_t width = ref_feature_in.size(3);
  const int64_t num_depth = depth.size(1);
  const bool has_weights = view_weights.defined();

  std::vector<torch::Tensor> weights;
  torch::Tensor ref_feature = ref_feature_in.view(
      {batch_size, num_groups, num_channels / num_groups, 1, height, width});
  torch::Tensor weight_sum =
      torch::zeros({batch_size, 1, 1, height, width}, torch::device(device));
  torch::Tensor similarity_sum =
      torch::zeros({batch_size, num_groups, num_depth, height, width},
                   torch::device(device));

  for (size_t i = 0; i < src_features.size(); ++i) {
    torch::Tensor warped_feature =
        DifferentiableWarping(src_features[i], src_proj_mtx[i], ref_proj_mtx,
                              depth)
            .view({batch_size, num_groups, num_channels / num_groups, num_depth,
                   height, width});
    torch::Tensor similarity = (warped_feature * ref_feature).mean(2);
    torch::Tensor view_weight = has_weights
                                    ? view_weights.select(1, i).unsqueeze(1)
                                    : pixelwise_net->forward(similarity);
    weights.push_back(view_weight);
    similarity_sum += similarity * view_weight.unsqueeze(1);
    weight_sum += view_weight.unsqueeze(1);
  }

  torch::Tensor score = torch::exp(torch::log_softmax(
      similarity_net->forward(similarity_sum / weight_sum, grid, weight), 1));
  depth = is_inverse ? InverseDepthRegression(depth, score)
                     : torch::sum(depth * score, 1);
  return std::tie(depth, score,
                  has_weights ? view_weights : torch::cat(weights, 1).detach());
}

torch::Tensor EvaluationImpl::DifferentiableWarping(
    const torch::Tensor& feature, const torch::Tensor& proj,
    const torch::Tensor& ref_proj, const torch::Tensor& depth) {
  torch::Device device = feature.device();
  const int64_t batch_size = feature.size(0);
  const int64_t num_channels = feature.size(1);
  const int64_t height = feature.size(2);
  const int64_t width = feature.size(3);
  const int64_t num_depth = depth.size(1);

  torch::Tensor proj_xy;
  {
    torch::NoGradGuard no_grad;

    torch::Tensor pmtx = torch::matmul(proj, torch::inverse(ref_proj));
    torch::Tensor rot = pmtx.narrow(1, 0, 3).narrow(2, 0, 3);
    torch::Tensor trans = pmtx.narrow(1, 0, 3).narrow(2, 3, 1);

    std::vector<torch::Tensor> xy = torch::meshgrid(
        {torch::arange(0.0, (double)height, torch::device(device)),
         torch::arange(0.0, (double)width, torch::device(device))});
    torch::Tensor x = xy[1].contiguous().view(height * width);
    torch::Tensor y = xy[0].contiguous().view(height * width);
    torch::Tensor xyz =
        torch::stack({x, y, torch::ones_like(x, torch::device(device))})
            .unsqueeze(0)
            .repeat({batch_size, 1, 1});
    xyz = torch::matmul(rot, xyz).unsqueeze(2).repeat({1, 1, num_depth, 1}) *
              depth.view({batch_size, 1, num_depth, height * width}) +
          trans.view({batch_size, 3, 1, 1});

    torch::Tensor mask = xyz.select(1, 2) <= 1e-3;
    xyz = torch::stack({xyz.select(1, 0).masked_fill(mask, (double)width),
                        xyz.select(1, 0).masked_fill(mask, (double)height),
                        xyz.select(1, 0).masked_fill(mask, 1.0)},
                       1);

    proj_xy = xyz.narrow(1, 0, 2) / xyz.select(1, 2);
    x = proj_xy.select(1, 0) / ((width - 1.0) / 2.0) - 1.0;
    y = proj_xy.select(1, 1) / ((height - 1.0) / 2.0) - 1.0;
    proj_xy = torch::stack({x, y}, 3);
  }

  return torch::nn::functional::grid_sample(feature, proj_xy, kGridSampleZeros)
      .view({batch_size, num_channels, num_depth, height, width});
}

torch::Tensor EvaluationImpl::InverseDepthRegression(
    const torch::Tensor& depth, const torch::Tensor& score) {
  const int64_t num_depth = depth.size(1);
  torch::Tensor depth_index =
      torch::arange(0.0, (double)num_depth, 1, torch::device(depth.device()))
          .view({1, num_depth, 1, 1});
  depth_index = torch::sum(depth_index * score, 1);
  const torch::Tensor inv_depth_min = 1.0 / depth.select(1, num_depth - 1);
  const torch::Tensor inv_depth_max = 1.0 / depth.select(1, 0);

  return 1.0 / (inv_depth_max + (inv_depth_min - inv_depth_max) * depth_index /
                                    (num_depth - 1));
}

PatchMatchModuleImpl::PatchMatchModuleImpl(
    const int propagation_range, const int iterations, const int num_samples,
    const double interval_scale, const int num_features,
    const int group_correlations, const int propagation_neighbors,
    const int evaluation_neighbors, const int stage)
    : Module("PatchMatchModule"),
      propagation_neighbors(propagation_neighbors),
      evaluation_neighbors(evaluation_neighbors),
      iterations(iterations),
      stage(stage),
      interval_scale(interval_scale) {
  CalcOffsets(propagation_range);
  if (propagation_neighbors > 0 && !(stage == 1 && iterations == 1)) {
    propagation_conv = register_module(
        "propagation_conv",
        torch::nn::Conv2d(
            torch::nn::Conv2dOptions(num_features, 2 * propagation_neighbors, 3)
                .stride(1)
                .padding(propagation_range)
                .dilation(propagation_range)
                .bias(true)));
  }
  evaluation_conv = register_module(
      "evaluation_conv",
      torch::nn::Conv2d(
          torch::nn::Conv2dOptions(num_features, 2 * evaluation_neighbors, 3)
              .stride(1)
              .padding(propagation_range)
              .dilation(propagation_range)
              .bias(true)));
  init_depth =
      register_module("init_depth", InitDepth(num_samples, interval_scale));
  propagation = register_module("propagation", Propagation());
  evaluation =
      register_module("evaluation", Evaluation(group_correlations, stage));
  feature_weight_net = register_module(
      "feture_weight_net",
      FeatureWeightNet(evaluation_neighbors, group_correlations));
}

std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
PatchMatchModuleImpl::forward(const torch::Tensor& ref_feature,
                              const torch::TensorList& src_features,
                              const torch::Tensor& ref_proj_mtx,
                              const torch::TensorList& src_proj_mtx,
                              const double depth_min, const double depth_max,
                              const torch::Tensor& depth_init,
                              const torch::Tensor& view_weights_init) {
  torch::Tensor depth = depth_init, view_weights = view_weights_init, score;
  torch::Device device = ref_feature.device();
  const int64_t batch_size = ref_feature.size(0);
  const int64_t height = ref_feature.size(2);
  const int64_t width = ref_feature.size(3);

  torch::Tensor propagation_grid;
  if (propagation_conv) {
    torch::Tensor propagation_offset =
        propagation_conv->forward(ref_feature)
            .view({batch_size, 2 * propagation_neighbors, height * width});
    propagation_grid =
        GetGrid(propagation_offset, prop_offset_orig, propagation_neighbors,
                batch_size, height, width, device);
  }

  torch::Tensor evaluation_offset =
      evaluation_conv->forward(ref_feature)
          .view({batch_size, 2 * evaluation_neighbors, height * width});
  torch::Tensor evaluation_grid =
      GetGrid(evaluation_offset, eval_offset_orig, evaluation_neighbors,
              batch_size, height, width, device);

  torch::Tensor feature_weight =
      feature_weight_net->forward(ref_feature, evaluation_grid);

  for (int iter = 0; iter < iterations; ++iter) {
    depth = init_depth->forward(depth, depth_min, depth_max, batch_size, height,
                                width, device);
    if (propagation_neighbors > 0 && !(stage == 1 && iter == iterations - 1)) {
      depth =
          propagation->forward(depth, propagation_grid, depth_min, depth_max);
    }
    torch::Tensor weight =
        GetDepthWeight(depth.detach(), evaluation_grid, depth_min, depth_max);
    weight *= feature_weight.unsqueeze(1);
    weight /= torch::sum(weight, 2, true);
    std::tie(depth, score, view_weights) =
        evaluation->forward(ref_feature, src_features, ref_proj_mtx,
                            src_proj_mtx, depth, evaluation_grid, weight,
                            view_weights, stage == 1 && iter == iterations - 1);
  }

  return {depth.unsqueeze(1).detach(), score, view_weights};
}

void PatchMatchModuleImpl::CalcOffsets(int dilation) {
  switch (propagation_neighbors) {
    case 0:
      break;
    case 4:
      prop_offset_orig = {
          {-dilation, 0}, {0, -dilation}, {0, dilation}, {dilation, 0}};
      break;
    case 8:
      prop_offset_orig = {{-dilation, -dilation}, {-dilation, 0},
                          {-dilation, dilation},  {0, -dilation},
                          {0, dilation},          {dilation, -dilation},
                          {dilation, 0},          {dilation, dilation}};
      break;
    case 16:
      prop_offset_orig = {{-dilation, -dilation}, {-dilation, 0},
                          {-dilation, dilation},  {0, -dilation},
                          {0, dilation},          {dilation, -dilation},
                          {dilation, 0},          {dilation, dilation}};
      for (size_t i = 0; i < 8; ++i) {
        const std::array<int, 2>& offset = prop_offset_orig[i];
        prop_offset_orig.push_back({2 * offset[0], 2 * offset[1]});
      }
      break;
    default:
      std::cout << "ERROR: Not implemented for " << propagation_neighbors
                << " propagation neighbors" << std::endl;
  }

  dilation--;
  switch (evaluation_neighbors) {
    case 9:
      eval_offset_orig = {
          {-dilation, -dilation}, {-dilation, 0}, {-dilation, dilation},
          {0, -dilation},         {0, 0},         {0, dilation},
          {dilation, -dilation},  {dilation, 0},  {dilation, dilation}};
      break;
    case 17:
      eval_offset_orig = {
          {-dilation, -dilation}, {-dilation, 0}, {-dilation, dilation},
          {0, -dilation},         {0, 0},         {0, dilation},
          {dilation, -dilation},  {dilation, 0},  {dilation, dilation}};
      for (size_t i = 0; i < 8; ++i) {
        const std::array<int, 2>& offset = prop_offset_orig[i];
        if (offset[0] == 0 && offset[1] == 0) continue;
        eval_offset_orig.push_back({2 * offset[0], 2 * offset[1]});
      }
      break;
    default:
      std::cout << "ERROR: Not implemented for " << evaluation_neighbors
                << " evaluation neighbors" << std::endl;
  }
}

torch::Tensor PatchMatchModuleImpl::GetGrid(
    const torch::Tensor& offset,
    const std::vector<std::array<int, 2>>& orig_offset, const int num_neighbors,
    const int64_t batch_size, const int64_t height, const int64_t width,
    torch::Device device) {
  torch::Tensor xy_grid;
  {
    torch::NoGradGuard no_grad;
    std::vector<torch::Tensor> grid = torch::meshgrid(
        {torch::arange(0.0, (double)height, torch::device(device)),
         torch::arange(0.0, (double)width, torch::device(device))});
    torch::Tensor y_grid = grid[0].contiguous().view(height * width);
    torch::Tensor x_grid = grid[1].contiguous().view(height * width);
    xy_grid =
        torch::stack({x_grid, y_grid}).unsqueeze(0).repeat({batch_size, 1, 1});
  }

  std::vector<torch::Tensor> xy_grids;
  for (int i = 0; i < orig_offset.size(); ++i) {
    torch::Tensor x_offset =
        orig_offset[i][1] + offset.select(1, 2 * i).unsqueeze(1);
    torch::Tensor y_offset =
        orig_offset[i][0] + offset.select(1, 2 * i + 1).unsqueeze(1);
    xy_grids.push_back(
        (xy_grid + torch::cat({x_offset, y_offset}, 1)).unsqueeze(2));
  }
  xy_grid = torch::cat(xy_grids, 2);

  torch::Tensor x_norm = xy_grid.select(1, 0) / ((width - 1.0) / 2.0) - 1;
  torch::Tensor y_norm = xy_grid.select(1, 1) / ((height - 1.0) / 2.0) - 1;
  return torch::stack({x_norm, y_norm}, 3)
      .view({batch_size, num_neighbors * height, width, 2});
}

torch::Tensor PatchMatchModuleImpl::GetDepthWeight(const torch::Tensor& depth,
                                                   const torch::Tensor& grid,
                                                   const double depth_min,
                                                   const double depth_max) {
  const int64_t batch_size = depth.size(0);
  const int64_t num_samples = depth.size(1);
  const int64_t height = depth.size(2);
  const int64_t width = depth.size(3);
  const double inv_depth_min = 1.0 / depth_min;
  const double inv_depth_max = 1.0 / depth_max;

  torch::Tensor weight = 1.0 / depth;
  weight = (weight - inv_depth_max) / (inv_depth_min - inv_depth_max);
  torch::Tensor grid_weight =
      torch::nn::functional::grid_sample(weight, grid, kGridSampleBorder)
          .view({batch_size, num_samples, evaluation_neighbors, height, width});
  grid_weight = torch::abs(grid_weight - weight.unsqueeze(2)) / interval_scale;
  return torch::sigmoid(2.0 * (2.0 - grid_weight.clamp(0.0, 4.0))).detach();
}

PatchMatchNetModuleImpl::PatchMatchNetModuleImpl(
    const std::unordered_map<std::string, std::string>& param_dict,
    const std::vector<double>& interval_scale,
    const std::vector<int>& propagation_range,
    const std::vector<int>& iterations, const std::vector<int>& num_samples,
    const std::vector<int>& propagation_neighbors,
    const std::vector<int>& evaluation_neighbors)
    : Module("PatchMatchNetModule"),
      param_dict(param_dict),
      num_depth(num_samples[0]) {
  feature = register_module("feature", FeatureNet());
  refinement = register_module("refinement", Refinement());

  patch_match.resize(kNumStages - 1, nullptr);
  const std::array<int, 3> num_features{16, 32, 64};
  const std::array<int, 3> group_correlations{4, 8, 8};
  for (int i = 0; i < kNumStages - 1; ++i) {
    const std::string module_name = "patch_match_" + std::to_string(i + 1);
    patch_match[i] = register_module(
        module_name,
        PatchMatchModule(propagation_range[i], iterations[i], num_samples[i],
                         interval_scale[i], num_features[i],
                         group_correlations[i], propagation_neighbors[i],
                         evaluation_neighbors[i], i + 1));
  }
}

std::tuple<torch::Tensor, torch::Tensor> PatchMatchNetModuleImpl::forward(
    const torch::Tensor& images, torch::Tensor& proj_matrices,
    const double depth_min, const double depth_max) {
  std::vector<torch::Tensor> ref_features = feature->forward(images.select(1, 0));
  std::vector<std::vector<torch::Tensor>> src_features(kNumStages);
  for (int im_idx = 1; im_idx < images.size(1); ++im_idx) {
    std::vector<torch::Tensor> stage_features =
        feature->forward(images.select(1, im_idx));
    for (int stage = 1; stage < kNumStages; ++stage) {
      src_features[stage].push_back(stage_features[stage]);
    }
  }

  torch::Tensor view_weights;
  torch::Tensor depth;
  torch::Tensor score;
  for (size_t stage = kNumStages - 1; stage > 0; --stage) {
    torch::Tensor ref_proj_mtx = proj_matrices.select(1, stage).select(1, 0);
    std::vector<torch::Tensor> src_proj_mtx =
        proj_matrices.select(1, stage).slice(1, 1).unbind(1);
    std::tie(depth, score, view_weights) = patch_match[stage - 1]->forward(
        ref_features[stage], src_features[stage], ref_proj_mtx, src_proj_mtx,
        depth_min, depth_max, depth, view_weights);

    if (stage > 1) {
      depth = torch::nn::functional::interpolate(depth, kInterpBilinear);
      view_weights =
          torch::nn::functional::interpolate(view_weights, kInterpBilinear);
    }
  }

  depth =
      refinement->forward(images.select(1, 0), depth, depth_min, depth_max).contiguous();
  torch::Tensor conf = CalcConfidence(score).contiguous();
  return {depth, conf};
}

torch::Tensor PatchMatchNetModuleImpl::CalcConfidence(
    const torch::Tensor& score) {
  torch::Tensor score_sum =
      4.0 * torch::nn::functional::avg_pool3d(
                torch::nn::functional::pad(
                    score.unsqueeze(1),
                    torch::nn::functional::PadFuncOptions({0, 0, 0, 0, 1, 2})),
                torch::nn::functional::AvgPool3dFuncOptions({4, 1, 1})
                    .stride(1)
                    .padding(0))
                .squeeze(1);
  torch::Tensor depth_index =
      torch::arange((double)num_depth, torch::device(score.device()))
          .view({1, num_depth, 1, 1});
  depth_index = torch::sum(score * depth_index, 1, true)
                    .to(torch::kLong)
                    .clamp(0, num_depth - 1);
  return torch::nn::functional::interpolate(score_sum.gather(1, depth_index),
                                            kInterpBilinear)
      .squeeze(1);
}

void PatchMatchNetModuleImpl::save(
    torch::serialize::OutputArchive& archive) const {
  // Write module parameters
  for (const auto& param : named_parameters()) {
    archive.write(param.key(), param.value());
  }
  // Write buffers
  for (const auto& buffer : named_buffers()) {
    archive.write(buffer.key(), buffer.value(), true);
  }
}

static void ReadValues(
    torch::serialize::InputArchive& archive,
    torch::OrderedDict<std::string, torch::Tensor>& dict,
    const std::unordered_map<std::string, std::string>& param_dict,
    bool is_buffer = false) {
  // We are using a hack with a pre-computed dictionary to address the naming
  // differences between the python and C++ models
  for (auto& param : dict) {
    bool found = false;
    torch::Tensor value;
    if (param_dict.count(param.key()) > 0) {
      found = archive.try_read(param_dict.at(param.key()), value, is_buffer);
    }
    if (!found) {
      found = archive.try_read(param.key(), value, is_buffer);
    }
    if (found) {
      param->copy_(value);
    } else {
      std::string type = is_buffer ? "buffer" : "parameter";
      std::cout << "WARN: archive does not contain " << type << ": "
                << param.key() << std::endl;
    }
  }
}

void PatchMatchNetModuleImpl::load(torch::serialize::InputArchive& archive) {
  {
    torch::NoGradGuard no_grad;
    ReadValues(archive, named_parameters(), param_dict, false);
    ReadValues(archive, named_buffers(), param_dict, true);
  }
}

}  // namespace mvs
}  // namespace colmap