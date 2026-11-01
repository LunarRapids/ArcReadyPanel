#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <memory>
#include <map>
#include <tuple>
#include <deque>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <vector>
namespace arc {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT status){if(FAILED(status))throw std::runtime_error("Windows graphics API failed");}
struct GraphicsDevice {
 ComPtr<ID3D11Device> gpu;ComPtr<ID3D11DeviceContext> immediate;ComPtr<IDXGIDevice> dxgi;
 ComPtr<ID2D1Factory1> factory;ComPtr<ID2D1Device> d2d;ComPtr<IDWriteFactory> writing;
 explicit GraphicsDevice(IDXGIAdapter1* adapter){
  check(D3D11CreateDevice(adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&gpu,nullptr,&immediate));check(gpu.As(&dxgi));
  D2D1_FACTORY_OPTIONS options{};check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),&options,reinterpret_cast<void**>(factory.GetAddressOf())));
  check(factory->CreateDevice(dxgi.Get(),&d2d));check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(writing.GetAddressOf())));
 }
};
class Surface {
 std::shared_ptr<GraphicsDevice> device;ComPtr<IDXGISwapChain1> swap;ComPtr<ID2D1DeviceContext> context;
 ComPtr<IDCompositionDevice> composition;ComPtr<IDCompositionTarget> target;ComPtr<IDCompositionVisual> visual;ComPtr<ID2D1SolidColorBrush> brush;
 std::map<std::tuple<std::wstring,float,bool>,ComPtr<IDWriteTextFormat>> formats;
 ComPtr<ID2D1Bitmap> appIcon;
 UINT pixelWidth{},pixelHeight{};float renderDpi{};
public:
 bool light=false,textShadow=false,material=false;
 float layoutWidth=1060,layoutHeight=760;bool responsive=false;
 float layoutX(float x)const{return responsive&&x>=210?210+(x-210)*(layoutWidth-210)/850:x;}
 float inputX(float x)const{return responsive&&x>=210?210+(x-210)*850/(layoutWidth-210):x;}
 float layoutY(float y)const{return responsive?y*layoutHeight/760:y;}
 float inputY(float y)const{return responsive?y*760/layoutHeight:y;}
 void layoutRect(float& x,float& y,float& w,float& h)const{float oldW=w,oldH=h;float right=layoutX(x+w),cx=layoutX(x+w/2);x=layoutX(x);w=right-x;if(oldW<=48){w=oldW;x=cx-w/2;}float bottom=layoutY(y+h),cy=layoutY(y+h/2);y=layoutY(y);h=bottom-y;if(oldH<=80){h=oldH;y=cy-h/2;}}
 std::map<std::tuple<float,float,float,float>,ComPtr<ID2D1LinearGradientBrush>> materials;
 ID2D1LinearGradientBrush* materialBrush(D2D1_COLOR_F fill,float x,float y,float w,float h){auto key=std::make_tuple(fill.r,fill.g,fill.b,fill.a);auto it=materials.find(key);if(it==materials.end()){if(materials.size()>128)materials.clear();auto top=fill;top.r=std::min(1.f,top.r+.045f);top.g=std::min(1.f,top.g+.045f);top.b=std::min(1.f,top.b+.065f);D2D1_GRADIENT_STOP stops[]={{0,top},{1,fill}};ComPtr<ID2D1GradientStopCollection> collection;ComPtr<ID2D1LinearGradientBrush> gradient;check(context->CreateGradientStopCollection(stops,2,&collection));check(context->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x,y),D2D1::Point2F(x+w*.25f,y+h)),collection.Get(),&gradient));it=materials.emplace(key,std::move(gradient)).first;}it->second->SetStartPoint(D2D1::Point2F(x,y));it->second->SetEndPoint(D2D1::Point2F(x+w*.25f,y+h));return it->second.Get();}
 uint64_t presents{};
 Surface(HWND hwnd,float width,float height,UINT dpi,std::shared_ptr<GraphicsDevice> shared,float scale=1,UINT exactWidth=0,UINT exactHeight=0):device(std::move(shared)){
  renderDpi=dpi*scale;pixelWidth=static_cast<UINT>(std::ceil(width*renderDpi/96.f));pixelHeight=static_cast<UINT>(std::ceil(height*renderDpi/96.f));
  if(exactWidth)pixelWidth=exactWidth;if(exactHeight)pixelHeight=exactHeight;
  ComPtr<IDXGIAdapter> adapter;check(device->dxgi->GetAdapter(&adapter));ComPtr<IDXGIFactory2> factory;check(adapter->GetParent(IID_PPV_ARGS(&factory)));
  DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=pixelWidth;desc.Height=pixelHeight;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
  check(factory->CreateSwapChainForComposition(device->gpu.Get(),&desc,nullptr,&swap));check(device->d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&context));
  ComPtr<IDXGISurface> buffer;check(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)));ComPtr<ID2D1Bitmap1> bitmap;
  auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(desc.Format,D2D1_ALPHA_MODE_PREMULTIPLIED),renderDpi,renderDpi);
  check(context->CreateBitmapFromDxgiSurface(buffer.Get(),&properties,&bitmap));context->SetTarget(bitmap.Get());context->SetDpi(renderDpi,renderDpi);
  check(DCompositionCreateDevice(device->dxgi.Get(),IID_PPV_ARGS(&composition)));check(composition->CreateTargetForHwnd(hwnd,TRUE,&target));check(composition->CreateVisual(&visual));check(visual->SetContent(swap.Get()));check(target->SetRoot(visual.Get()));check(composition->Commit());
  check(context->CreateSolidColorBrush(D2D1::ColorF(1.f,1.f,1.f),&brush));context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
 }
 void resize(UINT width,UINT height,float dpi){if(width==pixelWidth&&height==pixelHeight&&dpi==renderDpi)return;context->SetTarget(nullptr);device->immediate->Flush();check(swap->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0));pixelWidth=width;pixelHeight=height;renderDpi=dpi;ComPtr<IDXGISurface> buffer;check(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)));ComPtr<ID2D1Bitmap1> bitmap;auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),dpi,dpi);check(context->CreateBitmapFromDxgiSurface(buffer.Get(),&properties,&bitmap));context->SetTarget(bitmap.Get());context->SetDpi(dpi,dpi);}
 void reveal(){BOOL enabled=TRUE;SystemParametersInfo(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);if(!enabled)return;
  ComPtr<IDCompositionAnimation> opacity;check(composition->CreateAnimation(&opacity));check(opacity->AddCubic(0,.92f,1.f,-4.166667f,5.787037f));check(opacity->End(.24,1.f));
  ComPtr<IDCompositionEffectGroup> effects;check(composition->CreateEffectGroup(&effects));check(effects->SetOpacity(opacity.Get()));check(visual->SetEffect(effects.Get()));
  // Reveal fades only: moving the root visual also moves the frame and looks like a shake.
  check(visual->SetTransform(static_cast<IDCompositionTransform*>(nullptr)));check(composition->Commit());
 }
 void icon(HICON handle,float x,float y,float width,float height){x=layoutX(x);y=layoutY(y+height/2)-height/2;if(!appIcon){ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> image;ComPtr<IWICFormatConverter> converter;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));check(factory->CreateBitmapFromHICON(handle,&image));check(factory->CreateFormatConverter(&converter));check(converter->Initialize(image.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));check(context->CreateBitmapFromWicBitmap(converter.Get(),nullptr,&appIcon));}context->DrawBitmap(appIcon.Get(),D2D1::RectF(x,y,x+width,y+height),1,D2D1_INTERPOLATION_MODE_LINEAR);}
 void begin(){context->BeginDraw();context->Clear(D2D1::ColorF(0.f,0.f,0.f,0.f));}
 void glass(float x,float y,float w,float h,float radius){layoutRect(x,y,w,h);
  auto rect=D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),radius,radius);
  // Cached material; edge lighting and a small static shadow need no backdrop readback.
  for(int i=3;i>0;--i){brush->SetColor(D2D1::ColorF(0.f,0.f,0.f,light?.012f:.025f));context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x-i,y+2,x+w+i,y+h+2+i),radius+i,radius+i),brush.Get());}
  auto fill=light?D2D1::ColorF(.97f,.98f,1.f,.72f):D2D1::ColorF(.075f,.10f,.15f,.76f);context->FillRoundedRectangle(rect,materialBrush(fill,x,y,w,h));
  brush->SetColor(light?D2D1::ColorF(1.f,1.f,1.f,.92f):D2D1::ColorF(.85f,.93f,1.f,.30f));context->DrawRoundedRectangle(rect,brush.Get(),1);
  auto inner=D2D1::RoundedRect(D2D1::RectF(x+1,y+1,x+w-1,y+h-1),std::max(0.f,radius-1),std::max(0.f,radius-1));brush->SetColor(light?D2D1::ColorF(.35f,.46f,.60f,.09f):D2D1::ColorF(0.f,0.f,0.f,.23f));context->DrawRoundedRectangle(inner,brush.Get(),1);
 }
 void end(bool present=true){check(context->EndDraw());if(present){check(swap->Present(1,0));++presents;}}
 void box(float x,float y,float w,float h,float radius,D2D1_COLOR_F fill,D2D1_COLOR_F stroke=D2D1::ColorF(.64f,.8f,1.f,.12f)){

  layoutRect(x,y,w,h);auto rect=D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),radius,radius);brush->SetColor(fill);if(material&&fill.a>.15f)context->FillRoundedRectangle(rect,materialBrush(fill,x,y,w,h));else context->FillRoundedRectangle(rect,brush.Get());if(stroke.a>0){brush->SetColor(stroke);context->DrawRoundedRectangle(rect,brush.Get(),1.f);if(material&&h>=30){brush->SetColor(light?D2D1::ColorF(1.f,1.f,1.f,.45f):D2D1::ColorF(.82f,.87f,1.f,.11f));context->DrawLine(D2D1::Point2F(x+radius,y+1),D2D1::Point2F(x+w-radius,y+1),brush.Get(),1.f);}}
 }
 void symbol(const std::wstring& value,float x,float y,float w,float h,D2D1_COLOR_F color){layoutRect(x,y,w,h);brush->SetColor(color);float cx=x+w/2,cy=y+h/2;if(value==L"×"){context->DrawLine(D2D1::Point2F(cx-5,cy-5),D2D1::Point2F(cx+5,cy+5),brush.Get(),1.5f);context->DrawLine(D2D1::Point2F(cx+5,cy-5),D2D1::Point2F(cx-5,cy+5),brush.Get(),1.5f);}else context->DrawLine(D2D1::Point2F(cx-5,cy),D2D1::Point2F(cx+5,cy),brush.Get(),1.5f);}
 void text(const std::wstring& value,float x,float y,float size,D2D1_COLOR_F color,const std::wstring& font=L"Segoe UI",float width=700,bool bold=false){
  width=layoutX(x+width)-layoutX(x);x=layoutX(x);y=layoutY(y+size*.775f)-size*.775f;if(formats.size()>256)formats.clear();auto key=std::make_tuple(font,size,bold);auto it=formats.find(key);if(it==formats.end()){ComPtr<IDWriteTextFormat> format;check(device->writing->CreateTextFormat(font.c_str(),nullptr,bold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&format));format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);it=formats.emplace(std::move(key),std::move(format)).first;}
  if(textShadow){brush->SetColor(light?D2D1::ColorF(1.f,1.f,1.f,.70f):D2D1::ColorF(0.f,0.f,0.f,.72f));context->DrawText(value.c_str(),static_cast<UINT32>(value.size()),it->second.Get(),D2D1::RectF(x+1,y+1,x+width+1,y+size*1.55f+1),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);}
  brush->SetColor(color);context->DrawText(value.c_str(),static_cast<UINT32>(value.size()),it->second.Get(),D2D1::RectF(x,y,x+width,y+size*1.55f),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
 }
 void graph(const std::deque<double>& data,float x,float y,float w,float h,D2D1_COLOR_F color,bool percentage=true,bool filled=false){
  layoutRect(x,y,w,h);if(data.size()<2)return;double max=percentage?100.0:*std::max_element(data.begin(),data.end());max=std::max(max,1.0);
  if(filled){ComPtr<ID2D1PathGeometry> shape;ComPtr<ID2D1GeometrySink> sink;check(device->factory->CreatePathGeometry(&shape));check(shape->Open(&sink));sink->BeginFigure(D2D1::Point2F(x,y+h),D2D1_FIGURE_BEGIN_FILLED);for(size_t i=0;i<data.size();++i)sink->AddLine(D2D1::Point2F(x+w*float(i)/float(data.size()-1),y+h-h*float(std::clamp(data[i]/max,0.0,1.0))));sink->AddLine(D2D1::Point2F(x+w,y+h));sink->EndFigure(D2D1_FIGURE_END_CLOSED);check(sink->Close());auto fill=color;fill.a*=.18f;brush->SetColor(fill);context->FillGeometry(shape.Get(),brush.Get());}brush->SetColor(color);
  for(size_t i=1;i<data.size();++i){float ax=x+w*static_cast<float>(i-1)/static_cast<float>(data.size()-1),bx=x+w*static_cast<float>(i)/static_cast<float>(data.size()-1);
   context->DrawLine(D2D1::Point2F(ax,y+h-h*static_cast<float>(std::clamp(data[i-1]/max,0.0,1.0))),D2D1::Point2F(bx,y+h-h*static_cast<float>(std::clamp(data[i]/max,0.0,1.0))),brush.Get(),1.5f);}
 }
 UINT heightPixels()const{return pixelHeight;}
 UINT widthPixels()const{return pixelWidth;}
 unsigned alphaAt(float x,float y){ComPtr<ID3D11Texture2D> texture;check(swap->GetBuffer(0,IID_PPV_ARGS(&texture)));D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;ComPtr<ID3D11Texture2D> staging;check(device->gpu->CreateTexture2D(&desc,nullptr,&staging));device->immediate->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(device->immediate->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));auto px=std::min(pixelWidth-1,static_cast<UINT>(x*renderDpi/96.f)),py=std::min(pixelHeight-1,static_cast<UINT>(y*renderDpi/96.f));auto value=static_cast<const unsigned char*>(mapped.pData)[py*mapped.RowPitch+px*4+3];device->immediate->Unmap(staging.Get(),0);return value;}
 // Explicit development-only render export. This readback is never used in normal UI/Overlay.
 void png(const std::wstring& path){
  ComPtr<ID3D11Texture2D> texture;check(swap->GetBuffer(0,IID_PPV_ARGS(&texture)));D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
  ComPtr<ID3D11Texture2D> staging;check(device->gpu->CreateTexture2D(&desc,nullptr,&staging));device->immediate->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(device->immediate->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
  try{ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;check(factory->CreateStream(&stream));check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
   ComPtr<IWICBitmapEncoder> encoder;check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;check(encoder->CreateNewFrame(&frame,nullptr));check(frame->Initialize(nullptr));check(frame->SetSize(pixelWidth,pixelHeight));auto format=GUID_WICPixelFormat32bppBGRA;check(frame->SetPixelFormat(&format));
   // Convert premultiplied composition pixels to straight alpha for PNG.
   std::vector<unsigned char> pixels(static_cast<size_t>(pixelWidth)*pixelHeight*4);for(UINT y=0;y<pixelHeight;++y){auto src=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch;auto dest=pixels.data()+static_cast<size_t>(y)*pixelWidth*4;for(UINT x=0;x<pixelWidth;++x){const unsigned alpha=src[x*4+3];for(unsigned c=0;c<3;++c)dest[x*4+c]=alpha?static_cast<unsigned char>(std::min(255u,unsigned(src[x*4+c])*255/alpha)):0;dest[x*4+3]=static_cast<unsigned char>(alpha);}}
   check(frame->WritePixels(pixelHeight,pixelWidth*4,static_cast<UINT>(pixels.size()),pixels.data()));check(frame->Commit());check(encoder->Commit());
  }catch(...){device->immediate->Unmap(staging.Get(),0);throw;}device->immediate->Unmap(staging.Get(),0);
 }
};
}
