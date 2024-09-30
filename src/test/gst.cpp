#include "cfgo/cfgo.hpp"
#include "cfgo/gst/gst.hpp"
#include "cfgo/gst/appsink.hpp"

#include "cpptrace/cpptrace.hpp"

#include <string>
#include <cstdlib>
#include <thread>
#include <csignal>
#include <set>
#include <fstream>
#include <sstream>
#include <cuda.h>
#include "cuda_fp16.h"
#include "cuda/api.hpp"
// #include "opencv2/core.hpp"
// #include "opencv2/core/cuda.hpp"
// #include "opencv2/core/cuda_stream_accessor.hpp"
// #include "opencv2/imgcodecs.hpp"
// #include "opencv2/imgproc.hpp"
#include "gst/cuda/gstcuda.h"
#include "cuda/gst-cu.cuh"
#include "onnxruntime_cxx_api.h"

volatile std::sig_atomic_t gCtrlCStatus = 0;

void signal_handler(int signal)
{
    gCtrlCStatus = signal;
}

class ControlCHandler
{
private:
    cfgo::close_chan m_closer;
    std::thread m_t;
public:
    ControlCHandler(cfgo::close_chan closer);
    ~ControlCHandler();
};

ControlCHandler::ControlCHandler(cfgo::close_chan closer): m_closer(closer)
{
    m_t = std::thread([closer = m_closer]() mutable {
        while (true)
        {
            if (gCtrlCStatus)
            {
                closer.close();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        spdlog::debug("ctl-c handler completed.");
    });
    m_t.detach();
    std::signal(SIGINT, signal_handler);
}

ControlCHandler::~ControlCHandler()
{}

// std::string cv_type2str(int type)
// {
//     std::string r;

//     uchar depth = type & CV_MAT_DEPTH_MASK;
//     uchar chans = 1 + (type >> CV_CN_SHIFT);

//     switch (depth)
//     {
//     case CV_8U:
//         r = "8U";
//         break;
//     case CV_8S:
//         r = "8S";
//         break;
//     case CV_16U:
//         r = "16U";
//         break;
//     case CV_16S:
//         r = "16S";
//         break;
//     case CV_32S:
//         r = "32S";
//         break;
//     case CV_32F:
//         r = "32F";
//         break;
//     case CV_64F:
//         r = "64F";
//         break;
//     default:
//         r = "User";
//         break;
//     }

//     r += "C";
//     r += (chans + '0');

//     return r;
// }

GstBuffer * allocate_buffer(GstElement * element, gpointer user_data)
{
    auto & self = cfgo::cast_shared_holder_ref<cfgo::gst::BufferPool>(user_data);
    return self->acquire_buffer();
}

struct track_data
{
    cfgo::StandardStrand strand;
    cfgo::close_chan closer;
};

void on_track(GstElement * element, CfgoBoxedTrack * track, gpointer user_data)
{
    auto & data = cfgo::cast_shared_holder_ref<track_data>(user_data);
    asio::co_spawn(
        data->strand, 
        cfgo::fix_async_lambda([closer = data->closer, track = track->ptr]() -> asio::awaitable<void> {
            auto begin = std::chrono::high_resolution_clock::now();
            while (true)
            {
                co_await cfgo::wait_timeout(std::chrono::seconds {1}, closer);
                auto elapse = std::chrono::high_resolution_clock::now() - begin;
                auto rtp_rec_speed = track.get_rtp_receives_bytes() * 1.0 / 1024 * 1000 * 1000 * 1000 / elapse.count();
                auto rtp_drop_speed = track.get_rtp_drops_bytes() * 1.0 / 1024 * 1000 * 1000 * 1000 / elapse.count();
                CFGO_INFO("rtp received: {:.1} KB/s; rtp droped: {:.1} KB/s", rtp_rec_speed, rtp_drop_speed);
            }
        }), 
        asio::detached
    );
}

void cfgosrc_deep_element_added_callback (
    GstBin * self,
    GstBin * sub_bin,
    GstElement * element,
    gpointer user_data) {
    if (g_str_equal(GST_ELEMENT_NAME(element), "rtpjitterbuffer0"))
    {
        gst_object_ref(element);
        auto & data = cfgo::cast_shared_holder_ref<track_data>(user_data);
        asio::co_spawn(
            data->strand, 
            cfgo::fix_async_lambda([element, closer = data->closer]() -> asio::awaitable<void> {
                DEFER({
                    gst_object_unref(element);
                });
                do
                {
                    co_await cfgo::wait_timeout(std::chrono::seconds {1}, closer);
                    GstStructure * stats = nullptr;
                    g_object_get(G_OBJECT (element), "stats", &stats, NULL);
                    if (stats)
                    {
                        guint64 num_pushed;
                        guint64 num_lost;
                        guint64 num_late;
                        guint64 num_duplicates;
                        guint64 avg_jitter;
                        if (gst_structure_get(stats,
                            "num-pushed", G_TYPE_UINT64, &num_pushed,
                            "num-lost", G_TYPE_UINT64, &num_lost,
                            "num-late", G_TYPE_UINT64, &num_late,
                            "num-duplicates", G_TYPE_UINT64, &num_duplicates,
                            "avg-jitter", G_TYPE_UINT64, &avg_jitter,
                            NULL
                        ))
                        {
                            CFGO_DEBUG("pushed: {}, lost: {}, late: {}, duplicates: {}, avg: {} ms", num_pushed, num_lost, num_late, num_duplicates, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::nanoseconds{avg_jitter}).count());
                        }
                    }
                } while (true);
            }), 
            asio::detached
        );
    }
    
}

auto main_task(cfgo::Client::Strand strand, cfgo::close_chan closer) -> asio::awaitable<void> {
    using namespace cfgo;
    auto token = co_await utils::get_token("localhost", 3100, "10000", "10000", "user10000", "parent", "room0", true);
    cfgo::Configuration conf {
        cfgo::SignalConfigure {
            "ws://localhost:8080/ws", token
        },
        rtc::Configuration {},
        cfgo::TrackConfigure {}
    };
    auto client_ptr = std::make_shared<Client>(conf, strand, closer);
    gst::Pipeline pipeline("test pipeline");
    pipeline.add_node("cfgosrc", "cfgosrc");
    auto decode_caps = gst_caps_from_string("video/x-raw(memory:CUDAMemory)");
    std::shared_ptr<gst::BufferPool> buffer_pool = std::make_shared<gst::BufferPool>(1600, 16, 48);
    std::shared_ptr<track_data> track_data_ptr = std::make_shared<track_data>(strand, closer);
    track_data_ptr->strand = strand;
    track_data_ptr->closer = closer;
    g_object_set(
        pipeline.require_node("cfgosrc").get(),
        "client",
        (gint64) cfgo::wrap_client(client_ptr),
        "pattern",
        R"({
            "op": 0,
            "children": [
                {
                    "op": 13,
                    "args": ["video"]
                },
                {
                    "op": 7,
                    "args": ["key", "0"]
                }
            ]
        })",
        "mode",
        GST_CFGO_SRC_MODE_DECODE,
        "decode_caps",
        decode_caps,
        NULL
    );
    gst_caps_unref(decode_caps);
    g_signal_connect_data(
        pipeline.require_node("cfgosrc").get(), 
        "buffer-allocate", 
        G_CALLBACK(allocate_buffer), 
        cfgo::make_shared_holder(buffer_pool), [](gpointer data, GClosure *closure) {
            cfgo::destroy_shared_holder<gst::BufferPool>(data);
        },
        GConnectFlags::G_CONNECT_DEFAULT
    );
    CfgoBoxedTrack * track_ptr = new CfgoBoxedTrack();
    g_signal_connect_data(
        pipeline.require_node("cfgosrc").get(),
        "on-track",
        G_CALLBACK(on_track),
        cfgo::make_shared_holder(track_data_ptr), [](gpointer data, GClosure *closure) {
            cfgo::destroy_shared_holder<track_data>(data);
        },
        GConnectFlags::G_CONNECT_DEFAULT
    );
    g_signal_connect_data(
        pipeline.require_node("cfgosrc").get(),
        "deep-element-added",
        G_CALLBACK(cfgosrc_deep_element_added_callback),
        cfgo::make_shared_holder(track_data_ptr), [](gpointer data, GClosure *closure) {
            cfgo::destroy_shared_holder<track_data>(data);
        },
        GConnectFlags::G_CONNECT_DEFAULT
    );
    pipeline.add_node("cudaconvertscale", "cudaconvertscale");
    g_object_set(
        pipeline.require_node("cudaconvertscale").get(),
        "add-borders",
        TRUE,
        NULL
    );
    pipeline.add_node("capsfilter", "capsfilter");
    auto target_caps = gst_caps_from_string("video/x-raw(memory:CUDAMemory), format = (string) { RGB }, width = (int) 224, height = (int) 224, pixel-aspect-ratio = (fraction) 1/1");
    g_object_set(
        pipeline.require_node("capsfilter").get(),
        "caps",
        target_caps,
        NULL
    );
    gst_caps_unref(target_caps);
    pipeline.add_node("appsink", "appsink");
    auto a_link1 = pipeline.link_async("cudaconvertscale", "src", "capsfilter", "sink");
    if (!a_link1)
    {
        spdlog::error("Unable to link from cudaconvertscale to capsfilter");
        co_return;
    }
    auto a_link2 = pipeline.link_async("capsfilter", "src", "appsink", "sink");
    if (!a_link2)
    {
        spdlog::error("Unable to link from capsfilter to appsink");
        co_return;
    }
    auto a_link0 = pipeline.link_async("cfgosrc", "decode_src_%u_%u_%u_%u", "cudaconvertscale", "sink");
    if (!a_link0)
    {
        spdlog::error("Unable to link from cfgosrc to cudaconvertscale");
        co_return;
    }
    pipeline.run();
    AsyncTasksAll<void> tasks(closer);
    tasks.add_task(fix_async_lambda([pipeline, a_link0, a_link1, a_link2](auto closer) -> asio::awaitable<void> {
        auto link0 = co_await a_link0->await(closer);
        if (!link0)
        {
            if (!closer.is_timeout())
            {
                spdlog::error("Unable to link from {} to {}", GST_ELEMENT_NAME(a_link0->src()), GST_ELEMENT_NAME(a_link0->tgt()));
            }
            co_return;
        }
        auto link1 = co_await a_link1->await(closer);
        if (!link1)
        {
            if (!closer.is_timeout())
            {
                spdlog::error("Unable to link from {} to {}", GST_ELEMENT_NAME(a_link1->src()), GST_ELEMENT_NAME(a_link1->tgt()));
            }
            co_return;
        }
        auto link2 = co_await a_link2->await(closer);
        if (!link2)
        {
            if (!closer.is_timeout())
            {
                spdlog::error("Unable to link from {} to {}", GST_ELEMENT_NAME(a_link2->src()), GST_ELEMENT_NAME(a_link2->tgt()));
            }
            co_return;
        }
        spdlog::debug("linked.");

        auto raw_appsink = pipeline.require_node("appsink");
        cfgo::gst::AppSink appsink(GST_APP_SINK(raw_appsink.get()), 2, 8, 16);
        appsink.init();
        unsigned char * d_ready_areas = nullptr;
        half * d_ai_input = nullptr;
        AsyncBlockerManager blocker_manager({});
        AsyncTasksAll<void> tasks(closer);
        GstCudaContext * gst_cuda_ctx = nullptr;
        gst_cuda_ensure_element_context(raw_appsink.get(), -1, &gst_cuda_ctx);
        gst_cuda_context_push(gst_cuda_ctx);
        cudaMalloc(&d_ready_areas, 1 * 16 * 3 * 224 * 224);
        cudaCheckErrors("malloc ready areas memory");
        DEFER({
            cudaFree(d_ready_areas);
        });
        cudaMalloc(&d_ai_input, 1 * 16 * 3 * 224 * 224 * sizeof(half));
        cudaCheckErrors("malloc ai input memory");
        DEFER({
            cudaFree(d_ai_input);
        });
        gst_cuda_context_pop(NULL);
        gst_context_ref(GST_CONTEXT(gst_cuda_ctx));
        tasks.add_task(fix_async_lambda([gst_cuda_ctx, blocker_manager, d_ready_areas, d_ai_input](close_chan closer) mutable -> asio::awaitable<void> {
            DEFER({
                gst_context_unref(GST_CONTEXT(gst_cuda_ctx));
            });
            try
            {
                cudaStream_t cuda_stream;
                int device_id;
                gst_cuda_context_push(gst_cuda_ctx);
                cudaStreamCreate(&cuda_stream);
                cudaCheckErrors("create stream");
                DEFER({
                    cudaStreamDestroy(cuda_stream);
                });
                cudaGetDevice(&device_id);
                cudaCheckErrors("get device");
                gst_cuda_context_pop(NULL);
                std::vector<AsyncBlocker> locked_blockers;
                manually_ptr<unique_void_chan> * sync_ch_ptr = make_manually_ptr<unique_void_chan>();

                Ort::Env env(ORT_LOGGING_LEVEL_INFO, "ai-demo");
                Ort::SessionOptions session_options {};
                OrtTensorRTProviderOptions trt_options {};
                trt_options.device_id = device_id;
                trt_options.trt_fp16_enable = 1;
                trt_options.trt_max_workspace_size = (size_t) 10 * 1024 * 1024 * 1024;
                session_options.AppendExecutionProvider_TensorRT(trt_options);
                OrtCUDAProviderOptions cuda_options {};
                cuda_options.device_id = device_id;
                session_options.AppendExecutionProvider_CUDA(cuda_options);
                unique_void_chan session_ch {};
                Ort::Session session {nullptr};
                std::thread t0([&]() mutable {
                    CFGO_DEBUG("creating session...");
                    session = Ort::Session(env, (getexedir() / "model_fp16.onnx").c_str(), session_options);
                    chan_must_write(session_ch);
                    CFGO_DEBUG("session created.");
                });
                t0.detach();
                co_await session_ch.read();
                auto cuda_memory_info = Ort::MemoryInfo("Cuda", OrtAllocatorType::OrtArenaAllocator, device_id, OrtMemType::OrtMemTypeDefault);
                const int64_t shape[] = {1, 3, 16, 224, 224};
                const char * input_names[] = {"frames"};
                const char * output_names[] = {"output"};
                unique_void_chan ready_ch {};
                std::thread t([&]() mutable {
                    CFGO_DEBUG("warmup...");
                    try
                    {
                        gst_cuda_context_push(gst_cuda_ctx);
                        DEFER({
                            gst_cuda_context_pop(nullptr);
                        });
                        auto input = Ort::Value::CreateTensor(
                            cuda_memory_info,
                            d_ai_input + 0 * 3 * 16 * 224 * 224,
                            1 * 3 * 16 * 224 * 224 * sizeof(half),
                            shape,
                            std::size(shape),
                            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
                        );
                        const Ort::Value input_values[] = {std::move(input)};
                        session.Run(Ort::RunOptions{nullptr}, input_names, input_values, std::size(input_values), output_names, std::size(output_names));
                        spdlog::debug("warmuped.");
                        chan_must_write(ready_ch);
                    }
                    catch(...)
                    {
                        spdlog::error(what());
                    }
                });
                t.detach();
                co_await ready_ch.read();

                std::vector<std::string> labels = {"书写", "使用电子设备", "假装书写", "假装阅读", "其他非学习行为", "玩玩具", "睡着", "背书", "阅读"};

                do
                {
                    bool ready = false;
                    {
                        spdlog::debug("lock");
                        co_await blocker_manager.lock(closer);
                        DEFER({
                            spdlog::debug("unlock");
                            blocker_manager.unlock();
                        });
                        blocker_manager.collect_locked_blocker(locked_blockers);
                        if (!locked_blockers.empty())
                        {
                            gst_cuda_context_push(gst_cuda_ctx);
                            cudaEvent_t start, end;
                            cudaEventCreate(&start);
                            cudaCheckErrors("create start event");
                            cudaEventCreate(&end);
                            cudaCheckErrors("create stop event");
                            cudaEventRecord(start, cuda_stream);
                            for (auto && blocker : locked_blockers)
                            {
                                int index = (int) blocker.get_integer_user_data();
                                gst::copy_ai_input(d_ready_areas, index, d_ai_input, 0, cuda_stream);
                            }
                            cudaEventRecord(end, cuda_stream);
                            sync_ch_ptr->ref();
                            cudaStreamAddCallback(cuda_stream, [](cudaStream_t stream, cudaError_t status, void * userData) {
                                manually_ptr<unique_void_chan> * sync_ch_ptr = (manually_ptr<unique_void_chan> * ) userData;
                                chan_must_write(sync_ch_ptr->data());
                                manually_ptr_unref(&sync_ch_ptr);
                            }, sync_ch_ptr, 0);
                            gst_cuda_context_pop(NULL);

                            co_await sync_ch_ptr->data().read();
                            float used = 0.0;
                            cudaEventElapsedTime(&used, start, end);
                            cudaEventDestroy(start);
                            cudaEventDestroy(end);
                            spdlog::debug("copy ai input use {} ms", used);
                            ready = true;
                        }
                    }
                    if (ready)
                    {
                        gst_cuda_context_push(gst_cuda_ctx);
                        auto input = Ort::Value::CreateTensor(
                            cuda_memory_info,
                            d_ai_input + 0 * 3 * 16 * 224 * 224,
                            1 * 3 * 16 * 224 * 224 * sizeof(half),
                            shape,
                            std::size(shape),
                            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
                        );
                        const Ort::Value input_values[] = {std::move(input)};
                        assert(std::size(input_values) == 1);
                        assert(std::size(output_names) == 1);
                        auto start = std::chrono::high_resolution_clock::now();
                        auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names, input_values, std::size(input_values), output_names, std::size(output_names));
                        auto end = std::chrono::high_resolution_clock::now();
                        spdlog::debug("infer use {} ms", std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() * 1.0f / 1000);
                        assert(outputs.size() == 1 && outputs.front().IsTensor());
                        auto & output = outputs.front();
                        const Ort::Float16_t * floatarr = output.GetTensorData<Ort::Float16_t>();
                        std::vector<std::pair<std::string, float>> scores;
                        for (size_t i = 0; i < labels.size(); i++)
                        {
                            scores.push_back(std::make_pair(labels[i], (floatarr + i)->ToFloat()));
                        }
                        std::sort(scores.begin(), scores.end(), [](const std::pair<std::string, float> & e1, const std::pair<std::string, float> & e2) {
                            return e1.second > e2.second;
                        });

                        std::stringstream ss;
                        for (size_t i = 0; i < scores.size(); i++)
                        {
                            ss << "[" << scores[i].first << "]: " << scores[i].second << ", ";
                        }
                        ss << std::endl;
                        spdlog::debug(ss.str());
                        
                        gst_cuda_context_pop(NULL);
                    }
                } while (true);
            }
            catch(const CancelError& e)
            {
                spdlog::debug("closed 0");
                std::rethrow_exception(std::current_exception());
            }
            catch(...)
            {
                spdlog::debug("error 0");
                std::rethrow_exception(std::current_exception()); 
            }
        }));
        tasks.add_task(fix_async_lambda([appsink, blocker_manager, d_ready_areas](close_chan closer) mutable -> asio::awaitable<void> {
            try
            {
                int frame = 0;
                AsyncBlocker blocker = co_await blocker_manager.add_blocker(0, closer);
                blocker.set_user_data((std::int64_t) 0);
                manually_ptr<unique_void_chan> * sync_ch_ptr = make_manually_ptr<unique_void_chan>();
                DEFER({
                    manually_ptr_unref(&sync_ch_ptr);
                });
                do
                {
                    cfgo::gst::GstSampleSPtr sample = co_await appsink.pull_sample(closer);
                    if (!sample)
                    {
                        spdlog::debug("no sample, so eos.");
                        break;
                    }
                    auto buffer = gst_sample_get_buffer(sample.get());
                    auto memory = gst_buffer_get_memory(buffer, 0);
                    if (!memory)
                    {
                        spdlog::debug("unable to get memory.");
                        continue;
                    }
                    DEFER({
                        gst_memory_unref(memory);
                    });
                    if (!gst_is_cuda_memory(memory))
                    {
                        spdlog::debug("memory is not cuda memory.");
                        continue;
                    }
                    auto cuda_mem = (GstCudaMemory *) memory;
                    bool need_release_stream = false;
                    auto stream = gst_cuda_memory_get_stream(cuda_mem);
                    if (!stream)
                    {
                        need_release_stream = true;
                        stream = gst_cuda_stream_new(cuda_mem->context);
                    }
                    DEFER({
                        if (need_release_stream)
                        {
                            gst_cuda_stream_unref(stream);
                        }
                    });

                    cudaEvent_t start, end;
                    {
                        auto caps = gst_sample_get_caps(sample.get());
                        GstVideoFrame cuda_frame;
                        GstVideoInfo info;
                        gst_video_info_from_caps(&info, caps);
                        gst_video_frame_map(&cuda_frame, &info, buffer, (GstMapFlags) (GST_MAP_READ | GST_MAP_CUDA));
                        DEFER({
                            gst_video_frame_unmap(&cuda_frame);
                        });

                        gst_cuda_context_push(cuda_mem->context);
                        CUstream cuda_stream = gst_cuda_stream_get_handle(stream);
                        cudaEventCreate(&start);
                        cudaCheckErrors("create start event");
                        cudaEventCreate(&end);
                        cudaCheckErrors("create stop event");
                        cudaEventRecord(start, cuda_stream);
                        gst::copy_frame((unsigned char *) GST_VIDEO_FRAME_PLANE_DATA(&cuda_frame, 0), 1024, d_ready_areas, frame % 16, 0, cuda_stream);
                        cudaEventRecord(end, cuda_stream);
                        sync_ch_ptr->ref();
                        cudaStreamAddCallback(cuda_stream, [](cudaStream_t stream, cudaError_t status, void * userData) {
                            manually_ptr<unique_void_chan> * sync_ch_ptr = (manually_ptr<unique_void_chan> * ) userData;
                            try
                            {
                                chan_must_write(sync_ch_ptr->data());
                            }
                            catch(...)
                            {
                                spdlog::error(what());
                            }
                            manually_ptr_unref(&sync_ch_ptr);
                        }, sync_ch_ptr, 0);
                        gst_cuda_context_pop(NULL);
                    }

                    co_await sync_ch_ptr->data().read();
                    float used = 0.0;
                    cudaEventElapsedTime(&used, start, end);
                    cudaEventDestroy(start);
                    cudaEventDestroy(end);
                    spdlog::debug("copy frame use {} ms", used);

                    if (frame >= 16)
                    {
                        co_await blocker_manager.wait_blocker(blocker.id(), closer);
                    }
                    ++frame;
                } while (true);
            }
            catch(const CancelError& e)
            {
                spdlog::debug("closed 1");
                std::rethrow_exception(std::current_exception());
            }
            catch(...)
            {
                spdlog::debug("error 1");
                std::rethrow_exception(std::current_exception()); 
            }
        }));
        try
        {
            co_await tasks.await();
        }
        catch(const CancelError& e)
        {
            spdlog::debug("closed 2");
            std::rethrow_exception(std::current_exception());
        }
        catch(...)
        {
            spdlog::debug("error 2");
            std::rethrow_exception(std::current_exception()); 
        }
 
        // std::thread t([pipeline]() {
        //     try
        //     {
        //         int index = 0;
        //         half * d_ai_input = nullptr;
        //         auto appsink = pipeline.require_node("appsink");
        //         do
        //         {
        //             GstSample * sample = nullptr;
        //             g_signal_emit_by_name(appsink.get(), "pull-sample", &sample);
        //             if (!sample)
        //             {
        //                 break;
        //             }
        //             DEFER({
        //                 gst_sample_unref(sample);
        //             });
        //             auto buffer = gst_sample_get_buffer(sample);
        //             auto memory = gst_buffer_get_memory(buffer, 0);
        //             if (!memory)
        //             {
        //                 continue;
        //             }
        //             DEFER({
        //                 gst_memory_unref(memory);
        //             });
        //             if (gst_is_cuda_memory(memory))
        //             {
        //                 auto caps = gst_sample_get_caps(sample);
        //                 GstVideoFrame cuda_frame;
        //                 GstVideoInfo info;
        //                 gst_video_info_from_caps(&info, caps);
        //                 gst_video_frame_map(&cuda_frame, &info, buffer, (GstMapFlags) (GST_MAP_READ | GST_MAP_CUDA));
        //                 spdlog::debug("n components: {}, n plane: {}, frame size: {}, frame width: {}, frame height: {}",
        //                     GST_VIDEO_FRAME_N_COMPONENTS(&cuda_frame),
        //                     GST_VIDEO_FRAME_N_PLANES(&cuda_frame),
        //                     GST_VIDEO_FRAME_SIZE(&cuda_frame),
        //                     GST_VIDEO_FRAME_WIDTH(&cuda_frame),
        //                     GST_VIDEO_FRAME_HEIGHT(&cuda_frame)
        //                 );
        //                 for (size_t i = 0; i < GST_VIDEO_FRAME_N_COMPONENTS(&cuda_frame); i++)
        //                 {
        //                     spdlog::debug("[component {}] depth: {}, width: {}, height: {}, offset: {}, stride: {}, plane: {}, poffset: {}, pstride: {}",
        //                         i,
        //                         GST_VIDEO_FRAME_COMP_DEPTH(&cuda_frame, i),
        //                         GST_VIDEO_FRAME_COMP_WIDTH(&cuda_frame, i),
        //                         GST_VIDEO_FRAME_COMP_HEIGHT(&cuda_frame, i),
        //                         GST_VIDEO_FRAME_COMP_OFFSET(&cuda_frame, i),
        //                         GST_VIDEO_FRAME_COMP_STRIDE(&cuda_frame, i),
        //                         GST_VIDEO_FRAME_COMP_PLANE(&cuda_frame, i),
        //                         GST_VIDEO_FRAME_COMP_POFFSET(&cuda_frame, i),
        //                         GST_VIDEO_FRAME_COMP_PSTRIDE(&cuda_frame, i)
        //                     );
        //                 }
        //                 for (size_t i = 0; i < GST_VIDEO_FRAME_N_PLANES(&cuda_frame); i++)
        //                 {
        //                     spdlog::debug("[plane {}] offset: {}, stride: {}",
        //                         i,
        //                         GST_VIDEO_FRAME_PLANE_OFFSET(&cuda_frame, i),
        //                         GST_VIDEO_FRAME_PLANE_STRIDE(&cuda_frame, i)
        //                     );
        //                 }
                        
                        
        //                 auto cuda_mem = (GstCudaMemory *) memory;
        //                 bool need_release_stream = false;
        //                 auto stream = gst_cuda_memory_get_stream(cuda_mem);
        //                 if (!stream)
        //                 {
        //                     need_release_stream = true;
        //                     stream = gst_cuda_stream_new(cuda_mem->context);
        //                 }
        //                 gst_cuda_context_push(cuda_mem->context);
        //                 if (!d_ai_input)
        //                 {
        //                     cudaMalloc(&d_ai_input, 224 * 224 * 3 * 16 * 2);
        //                 }

        //                 if (index < 16)
        //                 {
        //                     cudaStream_t cuda_stream = gst_cuda_stream_get_handle(stream);
        //                     cudaStreamSynchronize(cuda_stream);
        //                     auto now = std::chrono::high_resolution_clock::now();
        //                     gst::copy_ai_input(
        //                         (unsigned char *) GST_VIDEO_FRAME_PLANE_DATA(&cuda_frame, 0), 
        //                         1024, 
        //                         d_ai_input,
        //                         index,
        //                         cuda_stream
        //                     );
        //                     cudaStreamSynchronize(cuda_stream);
        //                     auto used = std::chrono::high_resolution_clock::now() - now;
        //                     spdlog::debug("move used: {} ms.", std::chrono::duration_cast<std::chrono::milliseconds>(used).count());
        //                 }
        //                 else
        //                 {
        //                     {
        //                         int device_id;
        //                         cudaGetDevice(&device_id);
        //                         Ort::Env env(ORT_LOGGING_LEVEL_INFO, "ai-demo");
        //                         Ort::SessionOptions session_options;
        //                         OrtTensorRTProviderOptions trt_options;
        //                         trt_options.device_id = device_id;
        //                         trt_options.trt_fp16_enable = true;
        //                         trt_options.trt_max_workspace_size = (size_t) 10 * 1024 * 1024 * 1024;
        //                         session_options.AppendExecutionProvider_TensorRT(trt_options);
        //                         OrtCUDAProviderOptions cuda_options;
        //                         cuda_options.device_id = device_id;
        //                         session_options.AppendExecutionProvider_CUDA(cuda_options);
        //                         auto session = Ort::Session(env, (getexedir() / "model_fp16.onnx").c_str(), session_options);
        //                         auto memory_info = Ort::MemoryInfo("Cuda", OrtAllocatorType::OrtArenaAllocator, device_id, OrtMemType::OrtMemTypeDefault);
        //                         const int64_t shape[] = {1, 3, 16, 224, 224};
        //                         auto input = Ort::Value::CreateTensor(
        //                             memory_info,
        //                             (void *) d_ai_input,
        //                             224 * 224 * 3 * 16 * 2,
        //                             shape,
        //                             5,
        //                             ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
        //                         );
        //                         const char * input_names[] = {"frames"};
        //                         const Ort::Value input_values[] = {std::move(input)};
        //                         const char * output_names[] = {"output"};
        //                         auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names, input_values, (size_t) 1, output_names, (size_t) 1);
        //                         for (size_t i = 0; i < 10; i++)
        //                         {
        //                             auto now = std::chrono::high_resolution_clock::now();
        //                             auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names, input_values, (size_t) 1, output_names, (size_t) 1);
        //                             auto used = std::chrono::high_resolution_clock::now() - now;
        //                             spdlog::debug("infer used: {} ms.", std::chrono::duration_cast<std::chrono::milliseconds>(used).count());
        //                             assert(outputs.size() == 1 && outputs.front().IsTensor());
        //                             auto & output = outputs.front();
        //                             auto type_info = output.GetTypeInfo();
        //                             auto t_info = type_info.GetTensorTypeAndShapeInfo();
        //                             std::cout << "output element type: " << t_info.GetElementType() << std::endl;
        //                             auto m_info = output.GetTensorMemoryInfo();
        //                             std::cout << "output device type: " << m_info.GetDeviceType() << ", memory type: " << m_info.GetMemoryType() << ", device id: " << m_info.GetDeviceId() << std::endl;
        //                             const Ort::Float16_t * floatarr = output.GetTensorData<Ort::Float16_t>();
        //                             for (size_t j = 0; j < 9; j++)
        //                             {
        //                                 std::cout << "Score for class [" << j << "] =  " << floatarr[j].ToFloat() << "; ";
        //                             }
        //                             std::cout << std::endl << std::flush;
        //                         }
                            
        //                     }
        //                     cudaFree(d_ai_input);
        //                 }
        //                 gst_cuda_context_pop(nullptr);
        //                 if (need_release_stream)
        //                 {
        //                     gst_cuda_stream_unref(stream);
        //                 }
        //                 if (index >= 16)
        //                 {                            
        //                     break;
        //                 }
        //                 ++index;
        //             }
        //         } while (true);
        //     }
        //     catch(...)
        //     {
        //         spdlog::error(what());
        //     }
        // });
        // t.detach();
    }));
    tasks.add_task(fix_async_lambda([pipeline](auto closer) mutable -> asio::awaitable<void> {
        co_await pipeline.await();
    }));
    try
    {
        co_await tasks.await();
    }
    catch(const CancelError& e)
    {
        spdlog::debug("closed 3");
        std::rethrow_exception(std::current_exception());
    }
    catch(...)
    {
        spdlog::debug("error 3");
        std::rethrow_exception(std::current_exception()); 
    }

    // pipeline.add_node("mp4mux", "mp4mux");
    // pipeline.add_node("filesink", "filesink");
    // g_object_set(
    //     pipeline.require_node("filesink").get(), 
    //     "location", 
    //     (cfgo::getexedir() / "out.mp4").c_str(), 
    //     NULL
    // );
    // auto a_link_cfgosrc_mp4mux = pipeline.link_async("cfgosrc", "parse_src_%u_%u_%u_%u", "mp4mux", "video_%u");
    // if (!a_link_cfgosrc_mp4mux)
    // {
    //     spdlog::error("Unable to link from cfgosrc to mp4mux");
    //     co_return;
    // }
    // auto a_link_mp4mux_fakesink = pipeline.link_async("mp4mux", "src", "filesink", "sink");
    // if (!a_link_mp4mux_fakesink)
    // {
    //     spdlog::error("Unable to link from mp4mux to filesink");
    //     co_return;
    // }
    // pipeline.run();
    // auto pad_cfgosrc_parse = co_await pipeline.await_pad("cfgosrc", "parse_src_%u_%u_%u_%u", {}, closer);
    // if (!pad_cfgosrc_parse)
    // {
    //     spdlog::error("Unable to got pad parse_src_%u_%u_%u_%u from cfgosrc.");
    //     co_return;
    // }
    // if (auto caps = cfgo::gst::steal_shared_gst_caps(gst_pad_get_allowed_caps(pad_cfgosrc_parse.get())))
    // {
    //     cfgo_gst_print_caps(caps.get(), "");
    //     if (cfgo::gst::caps_check_any(caps.get(), [](auto structure) -> bool {
    //         return g_str_has_prefix(gst_structure_get_name(structure), "video/");
    //     }))
    //     {
    //         spdlog::debug("Found pad {} with video data.", GST_PAD_NAME(pad_cfgosrc_parse.get()));
    //         auto async_link = pipeline.link_async("cfgosrc", "parse_src_%u_%u_%u_%u", "mp4mux", "video_%u");
    //         co_await async_link->await(closer);
    //     }
    //     else if (cfgo::gst::caps_check_any(caps.get(), [](auto structure) -> bool {
    //         return g_str_has_prefix(gst_structure_get_name(structure), "audio/");
    //     }))
    //     {
    //         spdlog::debug("Found pad {} with audio data.", GST_PAD_NAME(pad_cfgosrc_parse.get()));
    //         auto async_link = pipeline.link_async("cfgosrc", "parse_src_%u_%u_%u_%u", "mp4mux", "audio_%u");
    //         co_await async_link->await(closer);
    //     }
    //     else
    //     {
    //         spdlog::error("The pad {} which could not connect to mp4mux.", GST_PAD_NAME(pad_cfgosrc_parse.get()));
    //     }
    // }
    // else
    // {
    //     spdlog::error("Unable to got caps of pad {} from cfgosrc.", GST_PAD_NAME(pad_cfgosrc_parse.get()));
    //     co_return;
    // }
    // auto link_cfgosrc_mp4mux = co_await a_link_cfgosrc_mp4mux->await(closer);
    // if (!link_cfgosrc_mp4mux)
    // {
    //     spdlog::error("Unable to link from cfgosrc to mp4mux");
    //     co_return;
    // }
    // auto link_mp4mux_fakesink = co_await a_link_mp4mux_fakesink->await(closer);
    // if (!link_mp4mux_fakesink)
    // {
    //     spdlog::error("Unable to link from mp4mux to fakesink");
    //     co_return;
    // }
    co_return;
}

void debug_plugins()
{
    auto plugins = gst_registry_get_plugin_list(gst_registry_get());
    DEFER({
        gst_plugin_list_free(plugins);
    });
    g_print("PATH: %s\n", std::getenv("PATH"));
    g_print("GST_PLUGIN_PATH: %s\n", std::getenv("GST_PLUGIN_PATH"));
    int plugins_num = 0;
    while (plugins)
    {
        auto plugin = (GstPlugin *) (plugins->data);
        plugins = g_list_next(plugins);
        g_print("plugin: %s\n", gst_plugin_get_name(plugin));
        ++plugins_num;
    }
    g_print("found %d plugins\n", plugins_num);
}

int main(int argc, char **argv) {
    cpptrace::register_terminate_handler();
    gst_init(&argc, &argv);
    GST_PLUGIN_STATIC_REGISTER(cfgosrc);
    spdlog::set_level(spdlog::level::debug);
    cfgo::Log::instance().set_level(cfgo::Log::DEFAULT, spdlog::level::debug);
    cfgo::Log::instance().set_pattern(cfgo::Log::DEFAULT, "[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v");
    gst_debug_set_threshold_for_name("cfgosrc", GST_LEVEL_TRACE);

    debug_plugins();

    GstPluginFeature* nvdec = gst_registry_lookup_feature(gst_registry_get(), "nvh264dec");
    if (nvdec)
    {
        gst_plugin_feature_set_rank(nvdec, GST_RANK_PRIMARY + 2);
        gst_object_unref(nvdec);
    }

    auto loop = g_main_loop_new(nullptr, FALSE);
    std::thread loop_t([loop]() {
        g_main_loop_run(loop);
    });
    loop_t.detach();

    cfgo::close_chan closer;

    ControlCHandler ctrl_c_handler(closer);

    asio::io_context io_ctx {};
    auto strand = asio::make_strand(io_ctx);
    auto f = asio::co_spawn(strand, cfgo::fix_async_lambda([strand, closer, loop]() mutable -> asio::awaitable<void> {
        try
        {
            co_await main_task(strand, closer);
        }
        catch(const cfgo::CancelError & e)
        {
            spdlog::debug("closed");
        }
        catch(...)
        {
            closer.close();
            spdlog::error(cfgo::what());
        }
        g_main_loop_quit(loop);
    }), asio::use_future);
    io_ctx.run();
    spdlog::debug("main end");
    return 0;
}