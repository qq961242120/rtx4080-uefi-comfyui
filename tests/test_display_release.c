/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual adapter, actual CAP0003 edges and code bytes; EFI effects simulated. */
#include "../display_release.c"
#include "../format.h"
#include "cap3_graph.h"
#include "captured_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
enum MODE {OK,AGG_REMOVED,EDID_ORPHANS_REMOVED,BAD_NV_HASH,BAD_COMBO_HASH,BAD_EDID_HASH,BAD_TEXT_HASH,BAD_GFX_HASH,
 BAD_VERSION,BAD_STOP,BAD_CODE_TYPE,EXTRA_OWNER,BAD_PATH,WRONG_AGG,EDID_FAIL,NV_FAIL,EDID_NOT_REMOVED,PHYSICAL_NOT_REMOVED,
 INPUT_CHANGED,KEY_CHANGED,PHYSICAL_MODE_CHANGED,UNRELATED_LOST,NEW_UNRELATED,CLAIM_FAIL,ATTR_FAIL,DECODE_EXTRA,CLOSE_FAIL,COLD_CHANGED,
 STATUS_CLEAR,STATUS_BEFORE_FLR,STATUS_SET,CONTROL_DRIFT,LINK_DRIFT,COMMAND_DRIFT,NEW_OWNER,NEW_DUPLICATE,
 OWN_COUNT,OWN_MISSING,AGG_MODE_CHANGED,AGG_INTERFACE_CHANGED,FINAL_BAD_CHIP,POST2_GRAPH_CHANGE,POST2_CONSOLE_CHANGE,POST2_AGG_CHANGE,POST2_SNAPSHOT_ERROR,RUN2_COLLAPSE,RUN2_MODE_CHANGE,RUN2_INFO_CHANGE,RUN2_INFO_SIZE_CHANGE,RUN2_FB_BASE_CHANGE,RUN2_FB_SIZE_CHANGE,RUN2_INTERFACE_CHANGE,RUN2_HANDLE_CHANGE,RUN2_GRAPH_CHANGE,RUN2_CONSOLE_CHANGE,RUN2_PHYSICAL_LEFT,RUN2_LATER_DRIFT,RUN2_LATER_RESTORE,EARLY_COLLAPSE,LATE_COLLAPSE,SNAPSHOT_ERROR,INVALID_COUNT,MODE_COUNT};
static char Trace[65536];static size_t TraceBytes;
static unsigned Checks;static enum MODE Mode;
#define CHECK(x) do{Checks++;if(!(x)){fprintf(stderr,"display FAIL mode=%d line=%d %s\n",Mode,__LINE__,#x);exit(1);}}while(0)
GU_PLATFORM Gu;
static EFI_BOOT_SERVICES Bs;static EFI_SYSTEM_TABLE St;static EFI_PCI_IO_PROTOCOL Io;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Keys;static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Text;
static EFI_GRAPHICS_OUTPUT_PROTOCOL Gop,Agg,OtherAgg;static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GopMode,AggMode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION GopInfo;
static EFI_DRIVER_BINDING_PROTOCOL Bind[5];static EFI_LOADED_IMAGE_PROTOCOL Images[5];static UINT8 *Code[5];
static UINT32 Cfg[64];static UINT64 Attrs;static UINTN Stage,Stops,Enables,Closes;static BOOLEAN Exclusive,Cold,Drift;
static EDGE Extra[3];static UINTN ExtraCount;static UINT8 BadPath[sizeof(GopPath)];
static EFI_HANDLE H(UINTN id){return (EFI_HANDLE)(id+1);}
static EFI_HANDLE FixtureAgent(UINTN i){static const UINTN a[]={428,348,153,341,346};return H(a[i]);}
static EFI_HANDLE FixtureImage(UINTN i){static const UINTN a[]={428,347,153,340,345};return H(a[i]);}
VOID GuLog(const char *f,...){char line[2048];va_list a;va_start(a,f);size_t n=GuFormatV(line,sizeof(line),f,a);va_end(a);CHECK(n!=(size_t)-1&&n<sizeof(line));CHECK(TraceBytes+n+1<sizeof(Trace));memcpy(Trace+TraceBytes,line,n);TraceBytes+=n;Trace[TraceBytes]=0;}
VOID GuLogText(const char *s){(void)s;}
BOOLEAN GuBootLogOpen(VOID){return TRUE;}
BOOLEAN GuTargetConfig(const UINT32 *c,UINTN s,UINTN b,UINTN d,UINTN f){return c[0]==GU_BOARD_ID&&c[11]==GU_BOARD_SUBSYSTEM&&!s&&b==1&&!d&&!f;}
BOOLEAN GuAd103ColdEntry(VOID){return Cold;}
BOOLEAN GuSafeToReturn(VOID){return TRUE;}
UINT32 GuRead32(UINT32 off){switch(off){case 0:return Mode==FINAL_BAD_CHIP?0x194000a1:0x193000a1;case 0x110100:return 0x10;case 0x82381c:return 0x88888888;case 0x823820:return 8;case 0x823804:return 0xffffffff;default:return 0;}}
static EFI_STATUS EFIAPI KeyRead(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,EFI_INPUT_KEY *k){(void)p;(void)k;return EFI_NOT_READY;}
static EFI_STATUS EFIAPI KeyReset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,BOOLEAN v){(void)p;(void)v;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Free(VOID *p){free(p);return EFI_SUCCESS;}
/* Expected fake firmware effects defined from reference, independently of EdgeClass. */
static BOOLEAN Filter(const EDGE *e){
 if(Stage>=1&&e->handle==H(429)&&e->agent==H(153)&&Mode!=EDID_NOT_REMOVED)return TRUE;
 if(Stage>=1&&Mode==EDID_ORPHANS_REMOVED&&e->agent==H(153))return TRUE;
 if(Stage>=2&&Mode!=PHYSICAL_NOT_REMOVED&&Mode!=RUN2_PHYSICAL_LEFT&&(e->handle==H(403)||e->handle==H(429)))return TRUE;
 if(Stage>=2&&Mode==AGG_REMOVED&&e->handle==H(430))return TRUE;
 return FALSE;
}
static EFI_STATUS EFIAPI Locate(EFI_LOCATE_SEARCH_TYPE k,EFI_GUID *g,VOID *key,UINTN *n,EFI_HANDLE **out){
 (void)key;*out=NULL;
 if(k==AllHandles){if(Mode==SNAPSHOT_ERROR||(Stage==2&&Mode==POST2_SNAPSHOT_ERROR))return EFI_DEVICE_ERROR;*n=Mode==INVALID_COUNT?4097:480;*out=calloc(*n,sizeof(**out));CHECK(*out);for(UINTN i=0;i<*n;i++)(*out)[i]=H(i);return EFI_SUCCESS;}
 CHECK(k==ByProtocol&&Guid(g,&GopGuid));*n=0;*out=calloc(3,sizeof(**out));CHECK(*out);
 if(Stage<2||Mode==PHYSICAL_NOT_REMOVED||Mode==RUN2_PHYSICAL_LEFT)(*out)[(*n)++]=H(429);
 if(!(Stage>=2&&Mode==AGG_REMOVED))(*out)[(*n)++]=H(Mode==WRONG_AGG||(Stage==2&&Mode==RUN2_HANDLE_CHANGE)?479:430);
 if(!*n){free(*out);*out=NULL;return EFI_NOT_FOUND;}return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h,EFI_GUID *g,VOID **out){
 *out=NULL;
 if(Guid(g,&PathGuid)){if(h==H(403))*out=(VOID *)PciPath;else if(h==H(429)&&Stage<2)*out=(VOID *)(Mode==BAD_PATH?BadPath:GopPath);}
 if(Guid(g,&PciGuid)&&h==H(403))*out=&Io;
 if(Guid(g,&GopGuid)&&h==H(429)&&(Stage<2||Mode==PHYSICAL_NOT_REMOVED||Mode==RUN2_PHYSICAL_LEFT))*out=&Gop;
 if(Guid(g,&GopGuid)&&(h==H(430)||h==H(479)))*out=((Drift&&Mode==AGG_INTERFACE_CHANGED)||(Stage==2&&Mode==RUN2_INTERFACE_CHANGE))?&OtherAgg:&Agg;
 if(Guid(g,&InputGuid)&&h==H(344))*out=&Keys;
 if(Guid(g,&TextGuid)&&h==H(344))*out=&Text;
 for(UINTN i=0;i<5;i++){if(Guid(g,&BindingGuid)&&h==FixtureAgent(i))*out=&Bind[i];if(Guid(g,&ImageGuid)&&h==FixtureImage(i))*out=&Images[i];}
 return *out?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI Protocols(EFI_HANDLE h,EFI_GUID ***out,UINTN *n){
 EFI_GUID *unique[64];*n=0;
 for(UINTN i=0;i<ARRAY_SIZE(CapturedEdges);i++)if(CapturedEdges[i].handle==h&&!Filter(&CapturedEdges[i])){
  BOOLEAN seen=FALSE;for(UINTN j=0;j<*n;j++)if(Guid(unique[j],&CapturedEdges[i].protocol))seen=TRUE;
  if(!seen)unique[(*n)++]=(EFI_GUID *)&CapturedEdges[i].protocol;
 }
 if(Exclusive&&h==H(403)){BOOLEAN seen=FALSE;for(UINTN j=0;j<*n;j++)if(Guid(unique[j],&PciGuid))seen=TRUE;if(!seen)unique[(*n)++]=&PciGuid;}
 for(UINTN i=0;i<ExtraCount;i++)if(Extra[i].handle==h){BOOLEAN seen=FALSE;for(UINTN j=0;j<*n;j++)if(Guid(unique[j],&Extra[i].protocol))seen=TRUE;if(!seen)unique[(*n)++]=&Extra[i].protocol;}
 if(!*n)unique[(*n)++]=&PathGuid;*out=malloc(*n*sizeof(**out));CHECK(*out);memcpy(*out,unique,*n*sizeof(**out));return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Opens(EFI_HANDLE h,EFI_GUID *g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out,UINTN *n){
 *out=calloc(128,sizeof(**out));CHECK(*out);*n=0;
 for(UINTN i=0;i<ARRAY_SIZE(CapturedEdges);i++){
  const EDGE *e=&CapturedEdges[i];if(e->handle!=h||!Guid(g,&e->protocol)||Filter(e))continue;
  if(Stage&&Mode==UNRELATED_LOST&&i==0)continue;
  (*out)[(*n)++]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){e->agent,e->controller,e->attributes,e->count};
 }
 if(Exclusive&&h==H(403)&&Guid(g,&PciGuid)&&!(Drift&&Mode==OWN_MISSING))(*out)[(*n)++]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(478),NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE,Drift&&Mode==OWN_COUNT?2:1};
 for(UINTN i=0;i<ExtraCount;i++)if(Extra[i].handle==h&&Guid(g,&Extra[i].protocol)){EDGE *e=&Extra[i];(*out)[(*n)++]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){e->agent,e->controller,e->attributes,e->count};}
 CHECK(*n<=128);return EFI_SUCCESS;
}
static EFI_TPL EFIAPI Raise(EFI_TPL t){CHECK(t==TPL_NOTIFY);return TPL_APPLICATION;}
static VOID EFIAPI Restore(EFI_TPL t){CHECK(t==TPL_APPLICATION);}
static EFI_STATUS EFIAPI Location(EFI_PCI_IO_PROTOCOL *p,UINTN *s,UINTN *b,UINTN *d,UINTN *f){CHECK(p==&Io);*s=*d=*f=0;*b=1;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI ConfigRead(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN n,VOID *out){CHECK(p==&Io&&w==EfiPciIoWidthUint32&&!off&&n==64);memcpy(out,Cfg,sizeof(Cfg));return EFI_SUCCESS;}
static EFI_STATUS EFIAPI GetBar(EFI_PCI_IO_PROTOCOL *p,UINT8 i,UINT64 *support,VOID **out){CHECK(p==&Io&&!i);*support=0;EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=calloc(1,sizeof(*d));CHECK(d);d->Desc=0x8a;d->Len=43;d->AddrRangeMin=GU_BAR0_BASE;d->AddrRangeMax=GU_BAR0_BASE+0xffffff;d->AddrLen=0x1000000;*out=d;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Attributes(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out){
 CHECK(p==&Io);if(op==EfiPciIoAttributeOperationGet){CHECK(out&&!mask);*out=Attrs;return EFI_SUCCESS;}
 if(op==EfiPciIoAttributeOperationSupported){CHECK(out&&!mask);*out=EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY;return EFI_SUCCESS;}
 CHECK(op==EfiPciIoAttributeOperationEnable&&mask==(EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY)&&!out&&Exclusive);Enables++;
 if(Mode==ATTR_FAIL)return EFI_DEVICE_ERROR;Attrs|=mask;Cfg[1]|=3;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Open(EFI_HANDLE h,EFI_GUID *g,VOID **out,EFI_HANDLE a,EFI_HANDLE ctrl,UINT32 attrs){CHECK(h==H(403)&&Guid(g,&PciGuid)&&a==H(478)&&!ctrl&&attrs==EFI_OPEN_PROTOCOL_EXCLUSIVE&&Stage==2&&!Exclusive);if(Mode==CLAIM_FAIL)return EFI_ACCESS_DENIED;Exclusive=TRUE;*out=&Io;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Close(EFI_HANDLE h,EFI_GUID *g,EFI_HANDLE a,EFI_HANDLE ctrl){CHECK(h==H(403)&&Guid(g,&PciGuid)&&a==H(478)&&!ctrl&&Exclusive);Closes++;if(Mode==CLOSE_FAIL)return EFI_DEVICE_ERROR;Exclusive=FALSE;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Disconnect(EFI_HANDLE c,EFI_HANDLE a,EFI_HANDLE child){
 CHECK(!child&&Stops<2&&c==(Stops?H(403):H(429))&&a==(Stops?H(428):H(153)));Stops++;Stage=Stops;
 if(Stage==2){Cfg[1]&=~3u;Attrs=0;if(Mode==DECODE_EXTRA)Cfg[2]^=1;if(Mode==COLD_CHANGED)Cold=FALSE;}
 if(Mode==INPUT_CHANGED||(Stage==2&&Mode==POST2_CONSOLE_CHANGE))St.ConIn=NULL;
 if(Stage==2&&Mode==POST2_AGG_CHANGE)AggMode.MaxMode=0;
 if(Stage==1&&Mode==EARLY_COLLAPSE)AggMode.MaxMode=1;
 if(Stage==2&&Mode>=RUN2_COLLAPSE&&Mode<=RUN2_LATER_RESTORE){
  /* RUN0002: same handle/interface/info, zero FB, only MaxMode 14 -> 1. */
  AggMode.MaxMode=1;
  if(Mode==RUN2_MODE_CHANGE)AggMode.Mode=1;
  if(Mode==RUN2_INFO_CHANGE)AggMode.Info=NULL;
  if(Mode==RUN2_INFO_SIZE_CHANGE)AggMode.SizeOfInfo=1;
  if(Mode==RUN2_FB_BASE_CHANGE)AggMode.FrameBufferBase=0x4000000000;
  if(Mode==RUN2_FB_SIZE_CHANGE)AggMode.FrameBufferSize=4096;
  if(Mode==RUN2_CONSOLE_CHANGE)St.ConIn=NULL;
  if(Mode==RUN2_GRAPH_CHANGE){ExtraCount=1;Extra[0]=(EDGE){H(479),H(470),H(479),TextGuid,0x10,1};}
 }

 if(Stage==2&&Mode==POST2_GRAPH_CHANGE){ExtraCount=1;Extra[0]=(EDGE){H(479),H(470),H(479),TextGuid,0x10,1};}
 if(Mode==KEY_CHANGED)Keys.ReadKeyStroke=NULL;
 if(Mode==PHYSICAL_MODE_CHANGED)GopInfo.HorizontalResolution++;
 if(Mode==NEW_UNRELATED){ExtraCount=1;Extra[0]=(EDGE){H(479),H(470),H(479),TextGuid,0x10,1};}
 if((Mode==EDID_FAIL&&Stage==1)||(Mode==NV_FAIL&&Stage==2))return EFI_DEVICE_ERROR;
 return EFI_SUCCESS;
}
static void Setup(void){
 TraceBytes=0;Trace[0]=0;
 memset(&Gu,0,sizeof(Gu));memset(&Plan,0,sizeof(Plan));memset(&Bs,0,sizeof(Bs));memset(&St,0,sizeof(St));memset(&Io,0,sizeof(Io));memset(&Keys,0,sizeof(Keys));
 memcpy(Cfg,CapturedConfig,sizeof(Cfg));memcpy(BadPath,GopPath,sizeof(GopPath));BadPath[28]^=1;Stage=Stops=Enables=Closes=ExtraCount=0;Exclusive=Drift=FALSE;Cold=TRUE;
 Bs.LocateHandleBuffer=Locate;Bs.HandleProtocol=Handle;Bs.ProtocolsPerHandle=Protocols;Bs.OpenProtocolInformation=Opens;Bs.FreePool=Free;Bs.RaiseTPL=Raise;Bs.RestoreTPL=Restore;Bs.OpenProtocol=Open;Bs.CloseProtocol=Close;Bs.DisconnectController=Disconnect;
 St.BootServices=&Bs;St.ConsoleInHandle=St.ConsoleOutHandle=St.StandardErrorHandle=H(344);St.ConIn=&Keys;St.ConOut=St.StdErr=&Text;Keys.ReadKeyStroke=KeyRead;Keys.Reset=KeyReset;Keys.WaitForKey=(EFI_EVENT)1;
 memset(&GopMode,0,sizeof(GopMode));memset(&GopInfo,0,sizeof(GopInfo));Gop.Mode=&GopMode;GopMode.Info=&GopInfo;GopMode.SizeOfInfo=sizeof(GopInfo);GopMode.FrameBufferBase=0x4000000000;GopMode.FrameBufferSize=0x1680000;GopMode.MaxMode=14;GopInfo.HorizontalResolution=2560;GopInfo.VerticalResolution=1440;
 memset(&AggMode,0,sizeof(AggMode));Agg.Mode=OtherAgg.Mode=&AggMode;AggMode.MaxMode=14;AggMode.Info=&GopInfo;/* observed info_bytes=0: do not dereference */
 Io.Pci.Read=ConfigRead;Io.GetLocation=Location;Io.GetBarAttributes=GetBar;Io.Attributes=Attributes;Attrs=EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY;
 Gu.bs=&Bs;Gu.pci=&Io;Gu.handle=H(403);Gu.image=H(478);Gu.profile_verified=Gu.prom_verified=TRUE;
 for(UINTN i=0;i<5;i++){const DRIVER_PIN *p=&DriverPins[i];memset(&Bind[i],0,sizeof(Bind[i]));memset(&Images[i],0,sizeof(Images[i]));Bind[i].Stop=(EFI_DRIVER_BINDING_STOP)(Code[i]+p->stop);Bind[i].Version=(UINT32)p->version;Bind[i].DriverBindingHandle=FixtureAgent(i);Bind[i].ImageHandle=FixtureImage(i);Images[i].ImageBase=Code[i];Images[i].ImageSize=p->size;Images[i].ImageCodeType=(EFI_MEMORY_TYPE)p->type;Images[i].FilePath=(VOID *)p->file;Images[i].DeviceHandle=i?H(6):H(403);}
 if(Mode==BAD_VERSION)Bind[0].Version++;
 if(Mode==BAD_STOP)Bind[1].Stop=(EFI_DRIVER_BINDING_STOP)(Code[1]+DriverPins[1].stop+1);
 if(Mode==BAD_CODE_TYPE)Images[2].ImageCodeType=EfiBootServicesCode;
 if(Mode==EXTRA_OWNER){ExtraCount=1;Extra[0]=(EDGE){H(403),H(470),H(403),PciGuid,0x10,1};}
}
int main(int argc,char **argv){
 CHECK(argc==2);static const UINTN ids[]={428,348,153,341,346};
 for(UINTN i=0;i<5;i++){char name[4096];snprintf(name,sizeof(name),"%s/driver-agent-%u-code.bin",argv[1],(unsigned)ids[i]);FILE *f=fopen(name,"rb");CHECK(f);Code[i]=calloc(1,DriverPins[i].size);CHECK(Code[i]);CHECK(fread(Code[i]+DriverPins[i].code,1,DriverPins[i].bytes,f)==DriverPins[i].bytes&&fgetc(f)==EOF);fclose(f);}
 for(Mode=OK;Mode<MODE_COUNT;Mode++){
  Setup();int hash=Mode>=BAD_NV_HASH&&Mode<=BAD_GFX_HASH?(int)Mode-BAD_NV_HASH:-1;if(hash>=0)Code[hash][DriverPins[hash].code]^=1;
  EFI_STATUS s=GuDisplayPreflight(&St,H(478),H(403));if(hash>=0)Code[hash][DriverPins[hash].code]^=1;
  if((Mode>=BAD_NV_HASH&&Mode<=WRONG_AGG)||Mode>=SNAPSHOT_ERROR){CHECK(s!=EFI_SUCCESS&&!Stops&&!GuDisplayMutated());printf("display admission %d PASS\n",Mode);continue;}
  CHECK(s==EFI_SUCCESS);CHECK(GuDisplayPreflight(&St,H(478),H(403))==EFI_ALREADY_STARTED);s=GuDisplayRelease();
  if(Mode>=EDID_FAIL&&Mode<=COLD_CHANGED&&Mode!=CLOSE_FAIL){CHECK(s!=EFI_SUCCESS&&GuDisplayMutated()&&Stops<=2);UINTN stopped=Stops;CHECK(GuDisplayRelease()!=EFI_SUCCESS&&Stops==stopped);CHECK(!GuDisplayReadyForWindows());printf("display partial failure %d PASS\n",Mode);continue;}
  if(Mode>=POST2_GRAPH_CHANGE&&Mode<=POST2_SNAPSHOT_ERROR){
   const char *tag=Mode==POST2_GRAPH_CHANGE?"check=graph":Mode==POST2_CONSOLE_CHANGE?"check=console":Mode==POST2_AGG_CHANGE?"check=GOP":"check=snapshot";
   CHECK(s==(Mode==POST2_SNAPSHOT_ERROR?EFI_DEVICE_ERROR:EFI_NOT_READY)&&Stops==2&&Enables==0&&!Exclusive&&GuDisplayMutated());
   CHECK(strstr(Trace,"AD103 post-detach failure: stage=2")&&strstr(Trace,tag));
   if(Mode==POST2_GRAPH_CHANGE)CHECK(strstr(Trace,"graph-new-edge"));
   if(Mode==POST2_AGG_CHANGE)CHECK(strstr(Trace,"aggregate-mode-diff")&&strstr(Trace,"max=14->0"));
   printf("display post2 diagnostic mode=%d %s PASS\n",Mode,tag);continue;
  }
  if((Mode>=RUN2_MODE_CHANGE&&Mode<=RUN2_PHYSICAL_LEFT)||Mode==EARLY_COLLAPSE){
   CHECK(s!=EFI_SUCCESS&&GuDisplayMutated()&&Stops==(Mode==EARLY_COLLAPSE?1u:2u)&&!Enables&&!Exclusive);
   CHECK(!GuDisplayReadyForWindows());printf("display run2 adversarial %d PASS\n",Mode);continue;
  }
  CHECK(s==EFI_SUCCESS&&Stops==2&&Enables==1&&Exclusive&&GuDisplayReleased());CHECK(GuDisplayRelease()!=EFI_SUCCESS&&Stops==2);
  if(Mode==RUN2_COLLAPSE||Mode==RUN2_LATER_DRIFT||Mode==RUN2_LATER_RESTORE){
   CHECK(AggMode.MaxMode==1&&strstr(Trace,"aggregate detached mode accepted")&&strstr(Trace,"max=14->1"));
  }
  if(Mode==LATE_COLLAPSE||Mode==RUN2_LATER_DRIFT||Mode==RUN2_LATER_RESTORE){
   AggMode.MaxMode=Mode==LATE_COLLAPSE?1:Mode==RUN2_LATER_DRIFT?2:14;
   CHECK(!GuDisplayReleased());CHECK(GuDisplayFinish()!=EFI_SUCCESS&&Exclusive&&!Closes);CHECK(!GuDisplayReadyForWindows());
   printf("display run2 later change %d refused PASS\n",Mode);continue;
  }

  if(Mode>=STATUS_CLEAR&&Mode<=AGG_INTERFACE_CHANGED){
   Drift=TRUE;
   if(Mode==STATUS_CLEAR||Mode==STATUS_BEFORE_FLR)Cfg[0x80/4]&=~0x90000u;
   if(Mode==STATUS_SET)Cfg[0x80/4]|=0x40000;
   if(Mode==CONTROL_DRIFT)Cfg[0x80/4]^=0x10;
   if(Mode==LINK_DRIFT)Cfg[0xa8/4]^=0x10000;
   if(Mode==COMMAND_DRIFT)Cfg[1]|=4;
   if(Mode==NEW_OWNER||Mode==NEW_DUPLICATE){ExtraCount=Mode==NEW_OWNER?1:2;Extra[0]=(EDGE){H(430),H(470),H(430),GopGuid,0x10,1};Extra[1]=Extra[0];}
   if(Mode==AGG_MODE_CHANGED)AggMode.MaxMode++;
   if(Mode==STATUS_CLEAR){CHECK(!GuDisplayReleased());Gu.flrs=1;CHECK(GuDisplayReleased());}
   else {CHECK(!GuDisplayReleased());CHECK(GuDisplayFinish()!=EFI_SUCCESS&&Exclusive&&!Closes);CHECK(!GuDisplayReadyForWindows());printf("display drift %d PASS\n",Mode);continue;}
  }
  Gu.quiesced=TRUE;Gu.flrs=1;s=GuDisplayFinish();
  if(Mode==CLOSE_FAIL){CHECK(s!=EFI_SUCCESS&&Exclusive&&Closes==1&&!GuDisplayReadyForWindows());}
  else {CHECK(s==EFI_SUCCESS&&!Exclusive&&!Gu.owned&&Closes==1);CHECK(GuDisplayReadyForWindows()==(Mode!=FINAL_BAD_CHIP));}
  printf("display success/finish %d PASS\n",Mode);
 }
 for(UINTN i=0;i<5;i++)free(Code[i]);printf("display: %u checks; %u scenarios; real CAP0003 graph/code, all EFI effects simulated.\n",Checks,MODE_COUNT);return 0;
}
