#pragma once
#include <windows.h>
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <optional>
#include <string>
#include <stdexcept>
#include <new>
#pragma warning(push)
#pragma warning(disable:4201)
#define ONEVPL_EXPERIMENTAL
#include "../third_party/vpl/mfxvideo.h"
#include "../third_party/vpl/mfxdispatcher.h"
#include "../third_party/vpl/mfxmemory.h"
#pragma warning(pop)
namespace arc {
using Microsoft::WRL::ComPtr;
inline void vplCheck(mfxStatus s,const char* operation){if(s<0||s==MFX_WRN_PARTIAL_ACCELERATION)throw std::runtime_error(std::string(operation)+" (oneVPL "+std::to_string(s)+")");}
struct ReplayDeviceError:std::runtime_error {HRESULT code;ReplayDeviceError(HRESULT value,const char* operation):std::runtime_error(std::string(operation)+" (HRESULT "+std::to_string(static_cast<unsigned>(value))+")"),code(value){}};
inline bool replayTransient(HRESULT code){return code==DXGI_ERROR_ACCESS_LOST||code==DXGI_ERROR_DEVICE_REMOVED||code==DXGI_ERROR_DEVICE_RESET||code==DXGI_ERROR_SESSION_DISCONNECTED||code==E_ACCESSDENIED||code==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;}
inline void replayCheck(HRESULT s,const char* operation){if(FAILED(s))throw ReplayDeviceError(s,operation);}
struct VplApi {
 HMODULE module{};
#define ARC_VPL(name) decltype(&name) name{};
 ARC_VPL(MFXLoad) ARC_VPL(MFXUnload) ARC_VPL(MFXCreateConfig) ARC_VPL(MFXSetConfigFilterProperty) ARC_VPL(MFXEnumImplementations) ARC_VPL(MFXDispReleaseImplDescription) ARC_VPL(MFXCreateSession) ARC_VPL(MFXClose)
 ARC_VPL(MFXVideoCORE_SetFrameAllocator) ARC_VPL(MFXVideoCORE_GetHandle) ARC_VPL(MFXVideoCORE_SetHandle) ARC_VPL(MFXVideoENCODE_Init) ARC_VPL(MFXVideoENCODE_Close) ARC_VPL(MFXVideoENCODE_EncodeFrameAsync) ARC_VPL(MFXVideoCORE_SyncOperation) ARC_VPL(MFXMemory_GetSurfaceForEncode) ARC_VPL(MFXVideoENCODE_GetVideoParam)
#undef ARC_VPL
 VplApi(){module=LoadLibraryExW(L"libvpl.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)throw std::runtime_error("Intel oneVPL runtime unavailable");try{
#define ARC_VPL(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(module,#name));if(!name)throw std::runtime_error("oneVPL export unavailable: " #name);
 ARC_VPL(MFXLoad) ARC_VPL(MFXUnload) ARC_VPL(MFXCreateConfig) ARC_VPL(MFXSetConfigFilterProperty) ARC_VPL(MFXEnumImplementations) ARC_VPL(MFXDispReleaseImplDescription) ARC_VPL(MFXCreateSession) ARC_VPL(MFXClose)
 ARC_VPL(MFXVideoCORE_SetFrameAllocator) ARC_VPL(MFXVideoCORE_GetHandle) ARC_VPL(MFXVideoCORE_SetHandle) ARC_VPL(MFXVideoENCODE_Init) ARC_VPL(MFXVideoENCODE_Close) ARC_VPL(MFXVideoENCODE_EncodeFrameAsync) ARC_VPL(MFXVideoCORE_SyncOperation) ARC_VPL(MFXMemory_GetSurfaceForEncode) ARC_VPL(MFXVideoENCODE_GetVideoParam)
#undef ARC_VPL
 }catch(...){FreeLibrary(module);module=nullptr;throw;}}
 ~VplApi(){if(module)FreeLibrary(module);}VplApi(const VplApi&)=delete;VplApi& operator=(const VplApi&)=delete;
};
struct EncodedFrame {uint64_t pts{};bool key{};std::vector<uint8_t> bytes;};
class VplEncoder {
 VplApi api;mfxLoader loader{};mfxSession session{};bool initialized=false;
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11VideoDevice> video;ComPtr<ID3D11VideoContext> videoContext;ComPtr<ID3D11VideoProcessorEnumerator> enumerator;ComPtr<ID3D11VideoProcessor> processor;
 ComPtr<ID3D11Texture2D> conversion;ComPtr<ID3D11VideoProcessorOutputView> outputView;
 std::vector<std::pair<ID3D11Texture2D*,ComPtr<ID3D11VideoProcessorInputView>>> inputViews;
 std::vector<std::pair<ID3D11Texture2D*,ComPtr<ID3D11VideoProcessorOutputView>>> directViews;bool directUnsupported=false;
 mfxFrameAllocator externalAllocator{};ComPtr<ID3D11Texture2D> externalTexture;mfxFrameSurface1 externalSurface{};
 static mfxStatus MFX_CDECL allocFrames(mfxHDL self,mfxFrameAllocRequest* request,mfxFrameAllocResponse* response){
  if(!self||!request||!response)return MFX_ERR_NULL_PTR;
  auto encoder=static_cast<VplEncoder*>(self);if(!request->NumFrameSuggested||(request->Type&MFX_MEMTYPE_SYSTEM_MEMORY)||request->Info.FourCC!=MFX_FOURCC_NV12)return MFX_ERR_UNSUPPORTED;
  auto mids=new(std::nothrow) mfxMemId[request->NumFrameSuggested]{};if(!mids)return MFX_ERR_MEMORY_ALLOC;
  D3D11_TEXTURE2D_DESC td{};td.Width=request->Info.Width;td.Height=request->Info.Height;td.ArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_NV12;td.BindFlags=D3D11_BIND_DECODER|D3D11_BIND_VIDEO_ENCODER|D3D11_BIND_SHADER_RESOURCE;
  for(unsigned i=0;i<request->NumFrameSuggested;++i){ID3D11Texture2D* t{};if(FAILED(encoder->device->CreateTexture2D(&td,nullptr,&t))){for(unsigned j=0;j<i;++j)static_cast<ID3D11Texture2D*>(mids[j])->Release();delete[] mids;return MFX_ERR_MEMORY_ALLOC;}mids[i]=t;}
  response->mids=mids;response->NumFrameActual=request->NumFrameSuggested;return MFX_ERR_NONE;
 }
 static mfxStatus MFX_CDECL lockFrame(mfxHDL,mfxMemId,mfxFrameData*){return MFX_ERR_UNSUPPORTED;}
 static mfxStatus MFX_CDECL unlockFrame(mfxHDL,mfxMemId,mfxFrameData*){return MFX_ERR_NONE;}
 // D3D11 allocator handles are texture/subresource pairs, as in Intel sample_common.
 static mfxStatus MFX_CDECL handleFrame(mfxHDL,mfxMemId mid,mfxHDL* handle){if(!mid||!handle)return MFX_ERR_NULL_PTR;auto pair=reinterpret_cast<mfxHDLPair*>(handle);pair->first=mid;pair->second=nullptr;return MFX_ERR_NONE;}
 static mfxStatus MFX_CDECL freeFrames(mfxHDL,mfxFrameAllocResponse* response){if(!response)return MFX_ERR_NULL_PTR;for(unsigned i=0;i<response->NumFrameActual;++i)static_cast<ID3D11Texture2D*>(response->mids[i])->Release();delete[] response->mids;*response={};return MFX_ERR_NONE;}

 bool externalEnabled=false;
 unsigned inputWidth=0,inputHeight=0,width,height,fps;uint64_t keyPts=0;bool keyIssued=false;std::vector<uint8_t> storage;
 void filter(const char* name,mfxU32 value){mfxVariant variant{};variant.Type=MFX_VARIANT_TYPE_U32;variant.Data.U32=value;auto config=api.MFXCreateConfig(loader);if(!config)throw std::runtime_error("oneVPL config failed");vplCheck(api.MFXSetConfigFilterProperty(config,reinterpret_cast<const mfxU8*>(name),variant),name);}
 void close(){directViews.clear();inputViews.clear();outputView.Reset();conversion.Reset();if(session){if(initialized)api.MFXVideoENCODE_Close(session);api.MFXClose(session);session=nullptr;}if(loader){api.MFXUnload(loader);loader=nullptr;}}
 void convert(ID3D11Texture2D* source,ID3D11Texture2D* target){
  D3D11_TEXTURE2D_DESC src{},dst{};source->GetDesc(&src);target->GetDesc(&dst);nativeWidth=dst.Width;nativeHeight=dst.Height;nativeBindFlags=dst.BindFlags;if(dst.ArraySize!=1)throw std::runtime_error("Unexpected oneVPL texture array");
  const bool configure=!processor||src.Width!=inputWidth||src.Height!=inputHeight;
  if(configure){directViews.clear();directUnsupported=false;inputViews.clear();outputView.Reset();processor.Reset();enumerator.Reset();D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};desc.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;desc.InputFrameRate={fps,1};desc.OutputFrameRate={fps,1};desc.InputWidth=src.Width;desc.InputHeight=src.Height;desc.OutputWidth=width;desc.OutputHeight=height;desc.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
   replayCheck(video->CreateVideoProcessorEnumerator(&desc,&enumerator),"video processor enumerator");replayCheck(video->CreateVideoProcessor(enumerator.Get(),0,&processor),"video processor");inputWidth=src.Width;inputHeight=src.Height;
   videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.Get(),0,FALSE);D3D11_VIDEO_PROCESSOR_COLOR_SPACE in{};in.RGB_Range=0;in.YCbCr_Matrix=1;D3D11_VIDEO_PROCESSOR_COLOR_SPACE out{};out.YCbCr_Matrix=1;out.Nominal_Range=1;videoContext->VideoProcessorSetStreamColorSpace(processor.Get(),0,&in);videoContext->VideoProcessorSetOutputColorSpace(processor.Get(),&out);
  }

  ID3D11VideoProcessorInputView* input=nullptr;for(auto& entry:inputViews)if(entry.first==source){input=entry.second.Get();break;}
  if(!input){if(inputViews.size()>=8)inputViews.clear();ComPtr<ID3D11VideoProcessorInputView> view;D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC id{};id.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;replayCheck(video->CreateVideoProcessorInputView(source,enumerator.Get(),&id,&view),"video input view");input=view.Get();inputViews.emplace_back(source,std::move(view));}

  if(configure){
  RECT sourceRect{0,0,static_cast<LONG>(src.Width),static_cast<LONG>(src.Height)},targetRect{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};double scale=std::min(double(width)/src.Width,double(height)/src.Height);LONG w=static_cast<LONG>(src.Width*scale)&~1L,h=static_cast<LONG>(src.Height*scale)&~1L;RECT dest{(static_cast<LONG>(width)-w)/2,(static_cast<LONG>(height)-h)/2,0,0};dest.right=dest.left+w;dest.bottom=dest.top+h;
  videoContext->VideoProcessorSetStreamSourceRect(processor.Get(),0,TRUE,&sourceRect);videoContext->VideoProcessorSetStreamDestRect(processor.Get(),0,TRUE,&dest);videoContext->VideoProcessorSetOutputTargetRect(processor.Get(),TRUE,&targetRect);D3D11_VIDEO_COLOR black{};black.YCbCr.Y=0;black.YCbCr.Cb=.5f;black.YCbCr.Cr=.5f;videoContext->VideoProcessorSetOutputBackgroundColor(processor.Get(),TRUE,&black);
  }
  D3D11_VIDEO_PROCESSOR_STREAM stream{};stream.Enable=TRUE;stream.pInputSurface=input;if(!directUnsupported){ID3D11VideoProcessorOutputView* direct=nullptr;for(auto& entry:directViews)if(entry.first==target){direct=entry.second.Get();break;}if(!direct){D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{};od.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;ComPtr<ID3D11VideoProcessorOutputView> view;auto result=video->CreateVideoProcessorOutputView(target,enumerator.Get(),&od,&view);if(SUCCEEDED(result)){if(directViews.size()>=8)directViews.clear();direct=view.Get();directViews.emplace_back(target,std::move(view));}else if(result==E_INVALIDARG||result==E_FAIL||result==E_NOTIMPL)directUnsupported=true;else replayCheck(result,"direct encoder output view");}if(direct){auto result=videoContext->VideoProcessorBlt(processor.Get(),direct,0,1,&stream);if(SUCCEEDED(result)){++directFrames;context->Flush();return;}if(result==E_INVALIDARG||result==E_FAIL||result==E_NOTIMPL){directUnsupported=true;directViews.clear();}else replayCheck(result,"direct encoder conversion");}}
  if(!conversion){auto desc=dst;desc.BindFlags=D3D11_BIND_RENDER_TARGET;desc.MiscFlags=0;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;replayCheck(device->CreateTexture2D(&desc,nullptr,&conversion),"NV12 conversion texture");}if(!outputView){D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{};od.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;replayCheck(video->CreateVideoProcessorOutputView(conversion.Get(),enumerator.Get(),&od,&outputView),"video output view");}
  replayCheck(videoContext->VideoProcessorBlt(processor.Get(),outputView.Get(),0,1,&stream),"video conversion");context->CopyResource(target,conversion.Get());++copyFrames;context->Flush();
 }
public:
 uint64_t directFrames=0,copyFrames=0;unsigned nativeWidth=0,nativeHeight=0,nativeBindFlags=0;
 VplEncoder(ID3D11Device* gpu,ID3D11DeviceContext* immediate,LUID luid,unsigned w,unsigned h,unsigned rate,unsigned mbps,bool applicationOwned=true):device(gpu),context(immediate),width(w),height(h),fps(rate),storage(4*1024*1024){try{
  ComPtr<ID3D10Multithread> threading;replayCheck(device.As(&threading),"D3D11 threading");threading->SetMultithreadProtected(TRUE);
  replayCheck(device.As(&video),"video device");replayCheck(context.As(&videoContext),"video context");loader=api.MFXLoad();if(!loader)throw std::runtime_error("oneVPL loader failed");filter("mfxImplDescription.Impl",MFX_IMPL_TYPE_HARDWARE);filter("mfxImplDescription.AccelerationMode",MFX_ACCEL_MODE_VIA_D3D11);filter("mfxImplDescription.VendorID",0x8086);filter("mfxImplDescription.mfxEncoderDescription.encoder.CodecID",MFX_CODEC_AV1);
  filter("mfxHandleType",MFX_HANDLE_D3D11_DEVICE);mfxVariant handleVariant{};handleVariant.Type=MFX_VARIANT_TYPE_PTR;handleVariant.Data.Ptr=device.Get();vplCheck(api.MFXSetConfigFilterProperty(api.MFXCreateConfig(loader),reinterpret_cast<const mfxU8*>("mfxHDL"),handleVariant),"set hardware device");
  mfxU32 implementationIndex=0;for(mfxU32 i=0;;++i){mfxHDL id{};auto status=api.MFXEnumImplementations(loader,i,MFX_IMPLCAPS_DEVICE_ID_EXTENDED,&id);if(status==MFX_ERR_NOT_FOUND)break;vplCheck(status,"enumerate Intel device");auto info=static_cast<mfxExtendedDeviceId*>(id);bool match=info->VendorID==0x8086&&info->LUIDValid&&memcmp(info->DeviceLUID,&luid,8)==0;api.MFXDispReleaseImplDescription(loader,id);if(match){implementationIndex=i;vplCheck(api.MFXCreateSession(loader,i,&session),"create Intel hardware session");break;}}
  if(!session)throw std::runtime_error("Intel Arc hardware AV1 implementation unavailable");
  mfxVideoParam param{};param.AsyncDepth=1;param.IOPattern=MFX_IOPATTERN_IN_VIDEO_MEMORY;auto& m=param.mfx;m.CodecId=MFX_CODEC_AV1;m.CodecProfile=MFX_PROFILE_AV1_MAIN;m.LowPower=MFX_CODINGOPTION_ON;m.TargetUsage=MFX_TARGETUSAGE_BEST_SPEED;m.RateControlMethod=MFX_RATECONTROL_CBR;m.BRCParamMultiplier=static_cast<mfxU16>((mbps*1000+65534)/65535);m.TargetKbps=static_cast<mfxU16>(mbps*1000/m.BRCParamMultiplier);m.GopPicSize=static_cast<mfxU16>(fps*2);m.GopRefDist=1;m.IdrInterval=1;
  auto& f=m.FrameInfo;f.FourCC=MFX_FOURCC_NV12;f.ChromaFormat=MFX_CHROMAFORMAT_YUV420;f.PicStruct=MFX_PICSTRUCT_PROGRESSIVE;f.Width=static_cast<mfxU16>((width+15)&~15u);f.Height=static_cast<mfxU16>((height+15)&~15u);f.CropW=static_cast<mfxU16>(width);f.CropH=static_cast<mfxU16>(height);f.FrameRateExtN=fps;f.FrameRateExtD=1;f.BitDepthLuma=8;f.BitDepthChroma=8;
  mfxExtAV1BitstreamParam av1{};av1.Header.BufferId=MFX_EXTBUFF_AV1_BITSTREAM_PARAM;av1.Header.BufferSz=sizeof(av1);av1.WriteIVFHeaders=MFX_CODINGOPTION_OFF;mfxExtBuffer* ext[]={&av1.Header};param.ExtParam=ext;param.NumExtParam=1;
  // Prefer an application-owned renderable input surface. The legacy allocator
  // exposes that exact texture to the encoder; raw pixels never visit the CPU.
  D3D11_TEXTURE2D_DESC td{};td.Width=f.Width;td.Height=f.Height;td.ArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_NV12;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_VIDEO_ENCODER;
  externalEnabled=applicationOwned&&SUCCEEDED(device->CreateTexture2D(&td,nullptr,&externalTexture));
  mfxStatus initStatus=MFX_ERR_UNSUPPORTED;
  if(externalEnabled){externalAllocator.pthis=this;externalAllocator.Alloc=allocFrames;externalAllocator.Lock=lockFrame;externalAllocator.Unlock=unlockFrame;externalAllocator.GetHDL=handleFrame;externalAllocator.Free=freeFrames;
   initStatus=api.MFXVideoCORE_SetFrameAllocator(session,&externalAllocator);if(initStatus>=0)initStatus=api.MFXVideoENCODE_Init(session,&param);
   if(initStatus<0||initStatus==MFX_WRN_PARTIAL_ACCELERATION){api.MFXVideoENCODE_Close(session);api.MFXClose(session);session=nullptr;externalTexture.Reset();externalEnabled=false;vplCheck(api.MFXCreateSession(loader,implementationIndex,&session),"recreate hardware session");}
  }
  if(!externalEnabled)initStatus=api.MFXVideoENCODE_Init(session,&param);
  vplCheck(initStatus,"initialize low-power AV1");initialized=true;
  if(externalEnabled){externalSurface.Info=f;externalSurface.Data.MemId=externalTexture.Get();}
vplCheck(api.MFXVideoENCODE_GetVideoParam(session,&param),"verify hardware encoder configuration");if(param.mfx.LowPower!=MFX_CODINGOPTION_ON||param.mfx.CodecId!=MFX_CODEC_AV1||param.IOPattern!=MFX_IOPATTERN_IN_VIDEO_MEMORY)throw std::runtime_error("Driver did not retain low-power video-memory AV1 configuration");

 }catch(...){close();throw;}}
 ~VplEncoder(){close();}
 std::optional<EncodedFrame> encode(ID3D11Texture2D* source,uint64_t pts){
  if(externalEnabled&&externalSurface.Data.Locked)throw std::runtime_error("Encoder retained input texture after synchronization");
  mfxFrameSurface1* surface=externalEnabled?&externalSurface:nullptr;
  if(!surface)vplCheck(api.MFXMemory_GetSurfaceForEncode(session,&surface),"allocate video surface");struct Guard{mfxFrameSurface1* s;~Guard(){if(s->FrameInterface)s->FrameInterface->Release(s);}} guard{surface};mfxHDL handle{};mfxResourceType type{};if(externalEnabled){handle=externalTexture.Get();type=MFX_RESOURCE_DX11_TEXTURE;}else vplCheck(surface->FrameInterface->GetNativeHandle(surface,&handle,&type),"get video surface");if(type!=MFX_RESOURCE_DX11_TEXTURE)throw std::runtime_error("AV1 surface is not video memory");convert(source,static_cast<ID3D11Texture2D*>(handle));surface->Data.TimeStamp=pts;
  mfxBitstream bits{};mfxSyncPoint sync{};mfxStatus status{};const auto deadline=GetTickCount64()+2000;mfxEncodeCtrl control{};bool forceKey=!keyIssued||pts-keyPts>=180000;if(forceKey)control.FrameType=MFX_FRAMETYPE_I|MFX_FRAMETYPE_IDR|MFX_FRAMETYPE_REF;
  do{bits={};sync=nullptr;bits.Data=storage.data();bits.MaxLength=static_cast<mfxU32>(storage.size());status=api.MFXVideoENCODE_EncodeFrameAsync(session,forceKey?&control:nullptr,surface,&bits,&sync);if(status==MFX_ERR_NOT_ENOUGH_BUFFER){if(storage.size()>=32*1024*1024)throw std::runtime_error("AV1 packet too large");storage.resize(storage.size()*2);}else if(status==MFX_WRN_DEVICE_BUSY)Sleep(1);else break;if(GetTickCount64()>deadline)throw std::runtime_error("AV1 encoder busy");}while(true);
  if(status==MFX_ERR_MORE_DATA){if(forceKey){keyPts=pts;keyIssued=true;}return {};}vplCheck(status,"encode AV1");if(forceKey){keyPts=pts;keyIssued=true;}if(!sync)return {};status=api.MFXVideoCORE_SyncOperation(session,sync,2000);vplCheck(status,"synchronize AV1");if(status==MFX_WRN_IN_EXECUTION)throw std::runtime_error("AV1 encode timed out");if(!bits.DataLength)return {};
  EncodedFrame frame;frame.pts=bits.TimeStamp==MFX_TIMESTAMP_UNKNOWN?pts:bits.TimeStamp;frame.key=(bits.FrameType&(MFX_FRAMETYPE_I|MFX_FRAMETYPE_IDR))!=0;frame.bytes.assign(storage.begin()+bits.DataOffset,storage.begin()+bits.DataOffset+bits.DataLength);return frame;
 }
};
}
