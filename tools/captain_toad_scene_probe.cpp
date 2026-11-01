// Render an imported opening through the embedded session and the real Reprise host.
#include "platform/gpu_commands.hpp"
#include "captain_toad_3ds/src/gpu/SdlDevice.hpp"
#include "captain_toad_3ds/src/session/Session.hpp"
#include <cstdio>
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("usage: captain_toad_scene_probe IMPORT OUTPUT");
        platform::Host host("Reprise opening scene check");
        std::unique_ptr<captain_toad::gpu::SdlDevice> device;
        std::unique_ptr<captain_toad::session::Session> session;
        auto draw=[&](const platform::GpuFrame& f) {
            platform::GpuCommands borrowed(f);
            // Flush SDL composition before the scene's own commands. No swapchain ownership.
            borrowed.submit();
            if(!device){device=std::make_unique<captain_toad::gpu::SdlDevice>(borrowed.device);captain_toad::gpu::setDevice(device.get());}
            device->begin(borrowed.color,borrowed.format,f.width,f.height);
            if(!session)session=std::make_unique<captain_toad::session::Session>(std::filesystem::path(argv[1])/"Opening");
            session->render(0,0,f.width,f.height);
            device->end();
        };
        std::filesystem::create_directories(argv[2]);
        host.capture(400,240,draw);
        for(int frame=0;frame<=360;++frame){
            if(frame)session->advance(1.0/60,{});
            if(frame%120==0){
                auto image=host.capture(400,240,draw);
                platform::save_png(image,std::filesystem::path(argv[2])/(std::to_string(frame)+".png"));
                std::printf("frame %d: %s\n",frame,session->status().c_str());
            }
        }
        session.reset();captain_toad::gpu::setDevice(nullptr);device.reset();
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
