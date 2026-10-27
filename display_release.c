/* SPDX-License-Identifier: GPL-2.0-only */
/* AD103 AD103 aggregate ownership adapter. Snapshot/delta strategy adapted from
 * impacifist/Windows-GPU-UNLOCK 9352abd internal-boot/boot/prepare-console.c.
 * Five driver identities/code were captured in CAP0003; aggregate release is based on
 * impacifist/Windows-GPU-UNLOCK e3f03cd prepare-console/full-release. No driver code/data patch.
 * No firmware variables, controller reconnects or reset fallback are used.
 */
#include "display_release.h"
#include "boot_io.h"
#include "profile.h"
#include "sha256.h"
#include "compat/compat.h"
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>
#include <IndustryStandard/Acpi10.h>

#define MAX_EDGES 4096
typedef struct {EFI_HANDLE handle,agent,controller;EFI_GUID protocol;UINT32 attributes,count;} EDGE;
typedef struct {UINTN count;EDGE edges[MAX_EDGES];} GRAPH;
typedef struct {
  EFI_SYSTEM_TABLE *table;EFI_HANDLE self,pci,gop,aggregate,out_agent,gfx_agent,nv_agent,combo_agent,edid_agent;
  EFI_HANDLE console,input,error;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *text,*err;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *keys;
  UINTN key_reset,key_read;EFI_EVENT key_wait;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop_interface,*aggregate_interface;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode,aggregate_mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
  UINT32 config[64];UINT64 attributes;
  UINTN stage;BOOLEAN ready,mutated,claimed;
} PLAN;
static PLAN Plan;
static GRAPH Original,Current;
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,TextGuid=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
static EFI_GUID InputGuid=EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
static EFI_GUID EdidGuid={0x1c0c34f6,0xd380,0x41fa,{0xa0,0x49,0x8a,0xd0,0x6c,0x1a,0x66,0xaa}};
static EFI_GUID BindingGuid=EFI_DRIVER_BINDING_PROTOCOL_GUID,ImageGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const UINT8 PciPath[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,1,1,1,6,0,0,0,127,255,4,0};
static const UINT8 GopPath[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,1,1,1,6,0,0,0,2,3,8,0,0,3,1,0x80,127,255,4,0};
#include "driver_pins.h"

static BOOLEAN Equal(const VOID *a,const VOID *b,UINTN n){return !memcmp(a,b,n);}
static BOOLEAN Guid(const EFI_GUID *a,const EFI_GUID *b){return Equal(a,b,16);}
static EFI_STATUS Interface(EFI_HANDLE h,EFI_GUID *g,VOID **p) {
  *p=NULL;EFI_STATUS s=Plan.table->BootServices->HandleProtocol(h,g,p);
  return s==EFI_SUCCESS&&!*p?EFI_COMPROMISED_DATA:s;
}
/* Match exact, bounded, single-instance paths; never accept a prefix alone. */
static BOOLEAN Path(EFI_HANDLE h,const UINT8 *bytes,UINTN count) {
  EFI_DEVICE_PATH_PROTOCOL *p=NULL;
  if(Interface(h,&PathGuid,(VOID **)&p)!=EFI_SUCCESS)return FALSE;
  UINTN at=0;
  while(at+4<=count) {
    UINTN n=p->Length[0]+((UINTN)p->Length[1]<<8);
    if(n<4||n>count-at||!Equal(p,bytes+at,n))return FALSE;
    if(p->Type==END_DEVICE_PATH_TYPE)return n==4&&p->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE&&at+4==count;
    at+=n;p=(VOID *)((UINT8 *)p+n);
  }
  return FALSE;
}
static EFI_STATUS Snapshot(GRAPH *g) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_HANDLE *handles=NULL;UINTN count=0;g->count=0;
  EFI_STATUS s=b->LocateHandleBuffer(AllHandles,NULL,NULL,&count,&handles);
  if(s==EFI_SUCCESS&&(!handles||!count||count>4096))s=EFI_BAD_BUFFER_SIZE;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_GUID **protocols=NULL;UINTN pc=0;
    s=b->ProtocolsPerHandle(handles[i],&protocols,&pc);
    if(s==EFI_SUCCESS&&(!protocols||!pc||pc>64))s=EFI_BAD_BUFFER_SIZE;
    for(UINTN j=0;s==EFI_SUCCESS&&j<pc;j++) {
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN ec=0;
      if(!protocols[j]){s=EFI_COMPROMISED_DATA;break;}
      s=b->OpenProtocolInformation(handles[i],protocols[j],&entries,&ec);
      if(s==EFI_SUCCESS&&(ec>128||(ec&&!entries)))s=EFI_BAD_BUFFER_SIZE;
      for(UINTN k=0;s==EFI_SUCCESS&&k<ec;k++) {
        EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=&entries[k];
        if(e->Attributes==1||e->Attributes==2||e->Attributes==4)continue;
        BOOLEAN own=Plan.claimed&&handles[i]==Plan.pci&&e->AgentHandle==Plan.self&&
          !e->ControllerHandle&&e->Attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE&&e->OpenCount==1;
        if(g->count==MAX_EDGES||!e->AgentHandle||(!e->ControllerHandle&&!own)||!e->OpenCount){s=EFI_COMPROMISED_DATA;break;}
        g->edges[g->count++]=(EDGE){handles[i],e->AgentHandle,e->ControllerHandle,*protocols[j],e->Attributes,e->OpenCount};
      }
      if(entries&&b->FreePool(entries)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
    }
    if(protocols&&b->FreePool(protocols)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  }
  if(handles&&b->FreePool(handles)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
static BOOLEAN SameEdge(const EDGE *a,const EDGE *b) {
  return a->handle==b->handle&&a->agent==b->agent&&a->controller==b->controller&&
    a->attributes==b->attributes&&a->count==b->count&&Guid(&a->protocol,&b->protocol);
}
/* Classes: 0 retained exactly; 1 original edge may disappear during known
 * aggregate/EDID cleanup; 2 must be gone. Never accept a new relationship. */
static UINTN EdgeClass(const EDGE *e,UINTN stage) {
  if(!stage)return 0;
  if(e->handle==Plan.gop&&e->agent==Plan.edid_agent)return 2;
  if(stage>=2&&(e->handle==Plan.pci&&e->agent==Plan.nv_agent))return 2;
  if(stage>=2&&(e->handle==Plan.gop||(e->agent==Plan.combo_agent&&e->handle!=Plan.pci)))return 2;
  if(e->agent==Plan.edid_agent||e->handle==Plan.aggregate)return 1;
  return 0;
}
static BOOLEAN OwnEdge(const EDGE *e) {
  return e->handle==Plan.pci&&e->agent==Plan.self&&!e->controller&&Guid(&e->protocol,&PciGuid)&&
    e->attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE&&e->count==1;
}
static BOOLEAN Expected(UINTN stage,BOOLEAN claimed) {
  UINTN own=0;
  for(UINTN i=0;i<Original.count;i++) {
    UINTN matches=0,kind=EdgeClass(&Original.edges[i],stage);
    for(UINTN j=0;j<Current.count;j++)if(SameEdge(&Original.edges[i],&Current.edges[j]))matches++;
    if(kind==0?matches!=1:kind==1?matches>1:matches!=0)return FALSE;
  }
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *e=&Current.edges[j];BOOLEAN known=FALSE;
    if(OwnEdge(e))own++;
    else {for(UINTN i=0;i<Original.count;i++)if(EdgeClass(&Original.edges[i],stage)!=2&&SameEdge(e,&Original.edges[i]))known=TRUE;
      if(!known)return FALSE;}
    for(UINTN k=j+1;k<Current.count;k++)if(SameEdge(e,&Current.edges[k]))return FALSE;
  }
  return own==(claimed?1u:0u);
}
#define NOTE(...) do{if(GuBootLogOpen())GuLog(__VA_ARGS__);}while(0)
/* Observability only: report bounded differences after returning to application TPL.
 * Do not call from Expected() because its pre-detach callers can hold TPL_NOTIFY.
 * No admission condition is relaxed and no additional firmware callback is made. */
static VOID EdgeNote(const char *reason,UINTN index,const EDGE *e,UINTN kind,UINTN matches) {
  UINT64 tail=0;for(UINTN i=0;i<8;i++)tail=(tail<<8)|e->protocol.Data4[i];
  NOTE("AD103 %s index=%zu class=%zu matches=%zu handle=%#llx agent=%#llx controller=%#llx guid=%#x-%#x-%#x-%#llx attrs=%#x count=%u\n",
    reason,(size_t)index,(size_t)kind,(size_t)matches,(unsigned long long)(UINTN)e->handle,
    (unsigned long long)(UINTN)e->agent,(unsigned long long)(UINTN)e->controller,
    e->protocol.Data1,(unsigned)e->protocol.Data2,(unsigned)e->protocol.Data3,
    (unsigned long long)tail,e->attributes,e->count);
}
static VOID GraphFailure(UINTN stage) {
  UINTN shown=0,total=0;
  for(UINTN i=0;i<Original.count;i++) {
    UINTN matches=0,kind=EdgeClass(&Original.edges[i],stage);
    for(UINTN j=0;j<Current.count;j++)if(SameEdge(&Original.edges[i],&Current.edges[j]))matches++;
    if(kind==0?matches!=1:kind==1?matches>1:matches!=0) {
      total++;if(shown++<8)EdgeNote("graph-original-edge",i,&Original.edges[i],kind,matches);
    }
  }
  for(UINTN j=0;j<Current.count;j++) {
    BOOLEAN known=FALSE;
    for(UINTN i=0;i<Original.count;i++)if(EdgeClass(&Original.edges[i],stage)!=2&&SameEdge(&Current.edges[j],&Original.edges[i]))known=TRUE;
    if(!known){total++;if(shown++<8)EdgeNote("graph-new-edge",j,&Current.edges[j],3,0);}
    for(UINTN k=0;k<j;k++)if(SameEdge(&Current.edges[j],&Current.edges[k])){
      total++;if(shown++<8)EdgeNote("graph-duplicate-edge",j,&Current.edges[j],4,k);break;
    }
  }
  NOTE("AD103 graph-difference-summary stage=%zu original=%zu current=%zu violations=%zu shown=%zu\n",
    (size_t)stage,(size_t)Original.count,(size_t)Current.count,(size_t)total,(size_t)(shown>8?8:shown));
}
static EFI_HANDLE Agent(UINTN i) {
  return i==0?Plan.nv_agent:i==1?Plan.combo_agent:i==2?Plan.edid_agent:i==3?Plan.out_agent:Plan.gfx_agent;
}
static EFI_STATUS Driver(UINTN which) {
  if(which>=5)return EFI_INVALID_PARAMETER;
  EFI_HANDLE a=Agent(which);const DRIVER_PIN *pin=&DriverPins[which];
  EFI_DRIVER_BINDING_PROTOCOL *d=NULL;EFI_LOADED_IMAGE_PROTOCOL *im=NULL;
  EFI_STATUS s=Interface(a,&BindingGuid,(VOID **)&d);
  if(s!=EFI_SUCCESS||!d->Stop||d->DriverBindingHandle!=a||!d->ImageHandle||d->Version!=pin->version)return EFI_UNSUPPORTED;
  s=Interface(d->ImageHandle,&ImageGuid,(VOID **)&im);if(s!=EFI_SUCCESS)return s;
  if(!im->ImageBase||im->ImageSize!=pin->size||im->ImageCodeType!=(EFI_MEMORY_TYPE)pin->type||
    (UINTN)d->Stop!=(UINTN)im->ImageBase+pin->stop||!im->FilePath||
    !Equal(im->FilePath,pin->file,pin->file_bytes)||pin->code+pin->bytes>pin->size||
    (which==0&&(im->DeviceHandle!=Plan.pci||d->ImageHandle!=a)))return EFI_UNSUPPORTED;
  return GuHashMatches((UINT8 *)im->ImageBase+pin->code,pin->bytes,pin->hash)?EFI_SUCCESS:EFI_CRC_ERROR;
}
static EFI_STATUS Alive(VOID) {
  EFI_SYSTEM_TABLE *t=Plan.table;VOID *in=NULL,*out=NULL,*err=NULL;
  if(t->ConsoleOutHandle!=Plan.console||t->ConsoleInHandle!=Plan.input||t->StandardErrorHandle!=Plan.error||
     t->ConIn!=Plan.keys||t->ConOut!=Plan.text||t->StdErr!=Plan.err||!t->ConIn||
     (UINTN)t->ConIn->Reset!=Plan.key_reset||(UINTN)t->ConIn->ReadKeyStroke!=Plan.key_read||
     t->ConIn->WaitForKey!=Plan.key_wait)return EFI_NOT_READY;
  EFI_STATUS s=Interface(Plan.input,&InputGuid,&in);
  if(s==EFI_SUCCESS)s=Interface(Plan.console,&TextGuid,&out);
  if(s==EFI_SUCCESS)s=Interface(Plan.error,&TextGuid,&err);
  return s==EFI_SUCCESS&&(in!=Plan.keys||out!=Plan.text||err!=Plan.err)?EFI_NOT_READY:s;
}
static EFI_STATUS Gops(BOOLEAN first,BOOLEAN detached_boundary) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_HANDLE *h=NULL;UINTN count=0,physical=0,aggregate=0;
  BOOLEAN rebase=FALSE;EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE detached_mode;
  EFI_STATUS s=b->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&h);
  if(Plan.stage>=2&&s==EFI_NOT_FOUND)return EFI_SUCCESS;
  if(s==EFI_SUCCESS&&(count>2||(count&&!h)))s=EFI_UNSUPPORTED;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;VOID *path=NULL;
    s=Interface(h[i],&GopGuid,(VOID **)&g);if(s!=EFI_SUCCESS)break;
    EFI_STATUS ps=b->HandleProtocol(h[i],&PathGuid,&path);
    if((ps==EFI_UNSUPPORTED||ps==EFI_NOT_FOUND)&&!path) {
      if(++aggregate!=1||!g->Mode||g->Mode->FrameBufferBase||g->Mode->FrameBufferSize||h[i]==Plan.console||h[i]==Plan.pci){s=EFI_UNSUPPORTED;break;}
      if(first){Plan.aggregate=h[i];Plan.aggregate_interface=g;Plan.aggregate_mode=*g->Mode;}
      else if(h[i]!=Plan.aggregate||g!=Plan.aggregate_interface||!Equal(g->Mode,&Plan.aggregate_mode,sizeof(Plan.aggregate_mode))) {
        /* RUN0002 on this exact board: removing the physical GOP reduced the
         * existing pathless, zero-FB aggregate MaxMode from 14 to 1.
         * Accept only at the immediate post-NVGOP boundary, after graph and
         * console validation. Every other byte and identity must still match.
         * Commit the new baseline only after the entire GOP scan succeeds. */
        EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE expected=Plan.aggregate_mode;expected.MaxMode=1;
        if(detached_boundary&&Plan.stage==2&&h[i]==Plan.aggregate&&g==Plan.aggregate_interface&&
           Plan.aggregate_mode.MaxMode==14&&!Plan.aggregate_mode.Mode&&Plan.aggregate_mode.Info&&
           !Plan.aggregate_mode.SizeOfInfo&&Equal(g->Mode,&expected,sizeof(expected))) {
          rebase=TRUE;detached_mode=*g->Mode;
        } else {
        NOTE("AD103 aggregate-mode-diff stage=%zu handle=%#llx->%#llx interface=%#llx->%#llx max=%u->%u mode=%u->%u info=%#llx->%#llx info_bytes=%zu->%zu fb=%#llx->%#llx fb_bytes=%zu->%zu\n",
          (size_t)Plan.stage,(unsigned long long)(UINTN)Plan.aggregate,(unsigned long long)(UINTN)h[i],
          (unsigned long long)(UINTN)Plan.aggregate_interface,(unsigned long long)(UINTN)g,
          Plan.aggregate_mode.MaxMode,g->Mode->MaxMode,Plan.aggregate_mode.Mode,g->Mode->Mode,
          (unsigned long long)(UINTN)Plan.aggregate_mode.Info,(unsigned long long)(UINTN)g->Mode->Info,
          (size_t)Plan.aggregate_mode.SizeOfInfo,(size_t)g->Mode->SizeOfInfo,
          (unsigned long long)Plan.aggregate_mode.FrameBufferBase,(unsigned long long)g->Mode->FrameBufferBase,
          (size_t)Plan.aggregate_mode.FrameBufferSize,(size_t)g->Mode->FrameBufferSize);
        s=EFI_NOT_READY;
        }
      }
      continue;
    }
    if(Plan.stage>=2||!Path(h[i],GopPath,sizeof(GopPath))||++physical!=1||!g->Mode||!g->Mode->Info||
      g->Mode->SizeOfInfo<sizeof(*g->Mode->Info)||g->Mode->Mode>=g->Mode->MaxMode||
      !g->Mode->FrameBufferBase||!g->Mode->FrameBufferSize){s=EFI_UNSUPPORTED;break;}
    if(first){Plan.gop=h[i];Plan.gop_interface=g;Plan.mode=*g->Mode;Plan.info=*g->Mode->Info;}
    else if(h[i]!=Plan.gop||g!=Plan.gop_interface||!Equal(g->Mode,&Plan.mode,sizeof(Plan.mode))||!Equal(g->Mode->Info,&Plan.info,sizeof(Plan.info)))s=EFI_NOT_READY;
  }
  if(h&&b->FreePool(h)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  if(s==EFI_SUCCESS&&(physical!=(Plan.stage>=2?0u:1u)||(!Plan.stage&&aggregate!=1)))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS&&rebase) {
    Plan.aggregate_mode=detached_mode;
    NOTE("AD103 aggregate detached mode accepted: stage=2 max=14->1; same interface, mode, info and zero FB; later checks remain strict\n");
  }
  return s;
}
static EFI_STATUS ReadConfig(UINT32 c[64]) {
  EFI_PCI_IO_PROTOCOL *io=NULL;UINTN seg,bus,dev,fun;
  EFI_STATUS s=Interface(Plan.pci,&PciGuid,(VOID **)&io);
  if(s!=EFI_SUCCESS||io!=Gu.pci||!io->Pci.Read||!io->GetLocation)return EFI_NOT_READY;
  s=io->GetLocation(io,&seg,&bus,&dev,&fun);
  if(s!=EFI_SUCCESS||seg||bus!=1||dev||fun)return EFI_UNSUPPORTED;
  s=io->Pci.Read(io,EfiPciIoWidthUint32,0,64,c);
  if(s==EFI_SUCCESS&&(!GuTargetConfig(c,seg,bus,dev,fun)||(c[1]&4)))s=EFI_UNSUPPORTED;
  return s;
}
/* Narrow upstream rule retained: only preexisting CED/URD may clear after FLR.
 * No link-speed, deemphasis, command, fatal-error or newly set bit exception. */
static BOOLEAN ConfigStable(const UINT32 c[64]) {
  for(UINTN i=0;i<64;i++) {
    UINT32 drift=c[i]^Plan.config[i];
    if(!drift)continue;
    BOOLEAN allowed=Gu.flrs==1&&i==0x80/4&&Plan.config[0x78/4]==0x0012b410&&
      !(c[i]&~Plan.config[i])&&!(drift&~0x00090000u);
    if(!allowed) {
      NOTE("PCI config drift refused: offset=%#x captured=%#x now=%#x bits=%#x flrs=%u\n",
        (unsigned)(i*4),Plan.config[i],c[i],drift,Gu.flrs);
      return FALSE;
    }
    NOTE("PCI status drift tolerated: offset=%#x captured=%#x now=%#x bits=%#x flrs=%u\n",
      (unsigned)(i*4),Plan.config[i],c[i],drift,Gu.flrs);
  }
  return TRUE;
}
static EFI_STATUS Bar(VOID) {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=NULL;UINT64 support=0;
  if(!Gu.pci->GetBarAttributes)return EFI_UNSUPPORTED;
  EFI_STATUS s=Gu.pci->GetBarAttributes(Gu.pci,0,&support,(VOID **)&d);
  if(s==EFI_SUCCESS&&(!d||d->Desc!=0x8a||d->Len!=43||d->ResType||
     d->AddrRangeMin!=GU_BAR0_BASE||d->AddrRangeMax!=0x90ffffff||
     d->AddrTranslationOffset||d->AddrLen!=0x1000000))s=EFI_UNSUPPORTED;
  if(d&&Plan.table->BootServices->FreePool(d)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
static EFI_STATUS Consumers(VOID) {
  UINTN pci=0,child=0,combo=0,agg=0,edid=0,text=0,console=0,gfx=0;
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->handle==Plan.pci) {
      if(e->count!=1)return EFI_ACCESS_DENIED;
      if(Guid(&e->protocol,&PciGuid)&&e->attributes==0x10&&e->controller==Plan.pci){Plan.nv_agent=e->agent;pci++;}
      else if(Guid(&e->protocol,&PathGuid)&&e->attributes==8&&e->controller==Plan.gop)child++;
      else return EFI_ACCESS_DENIED;
    } else if(e->handle==Plan.gop) {
      if(e->count!=1)return EFI_ACCESS_DENIED;
      if(Guid(&e->protocol,&GopGuid)&&e->attributes==0x10&&e->controller==Plan.gop){Plan.combo_agent=e->agent;combo++;}
      else if(Guid(&e->protocol,&GopGuid)&&e->attributes==8&&e->controller==Plan.aggregate)agg++;
      else if(Guid(&e->protocol,&EdidGuid)&&e->attributes==0x10&&e->controller==Plan.gop){Plan.edid_agent=e->agent;edid++;}
      else return EFI_ACCESS_DENIED;
    } else if(e->handle==Plan.aggregate) {
      if(e->count!=1)return EFI_ACCESS_DENIED;
      if(Guid(&e->protocol,&TextGuid)&&e->attributes==0x10&&e->controller==Plan.aggregate){Plan.out_agent=e->agent;text++;}
      else if(Guid(&e->protocol,&TextGuid)&&e->attributes==8&&e->controller==Plan.console)console++;
      else if(Guid(&e->protocol,&GopGuid)&&e->attributes==0x10&&e->controller==Plan.aggregate){Plan.gfx_agent=e->agent;gfx++;}
      else return EFI_ACCESS_DENIED;
    }
  }
  if(pci!=1||child!=1||combo!=1||agg!=1||edid!=1||text!=1||console!=1||gfx!=1)return EFI_ACCESS_DENIED;
  for(UINTN i=0;i<5;i++)for(UINTN j=i+1;j<5;j++)if(Agent(i)==Agent(j))return EFI_ACCESS_DENIED;
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->handle==Plan.pci&&e->agent!=Plan.nv_agent)return EFI_ACCESS_DENIED;
    if(e->handle==Plan.gop&&Guid(&e->protocol,&GopGuid)&&e->agent!=Plan.combo_agent)return EFI_ACCESS_DENIED;
    if(e->handle==Plan.aggregate&&Guid(&e->protocol,&TextGuid)&&e->agent!=Plan.out_agent)return EFI_ACCESS_DENIED;
    if((e->agent==Plan.nv_agent||e->agent==Plan.combo_agent)&&e->handle!=Plan.pci&&e->handle!=Plan.gop&&e->handle!=Plan.aggregate)return EFI_ACCESS_DENIED;
  }
  for(UINTN i=0;i<5;i++){EFI_STATUS s=Driver(i);if(s!=EFI_SUCCESS)return s;}
  return EFI_SUCCESS;
}
EFI_STATUS GuDisplayPreflight(EFI_SYSTEM_TABLE *t,EFI_HANDLE self,EFI_HANDLE pci) {
  if(Plan.mutated||Plan.ready)return EFI_ALREADY_STARTED;
  memset(&Plan,0,sizeof(Plan));Plan.table=t;Plan.self=self;Plan.pci=pci;
  if(!t||!t->BootServices||!self||!pci||Gu.handle!=pci||Gu.image!=self||Gu.bs!=t->BootServices||
     !Gu.profile_verified||!Gu.prom_verified||Gu.started||Gu.owned||Gu.armed)return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *b=t->BootServices;
  if(!b->DisconnectController||!b->OpenProtocol||!b->CloseProtocol||!b->RaiseTPL||!b->RestoreTPL||
     !b->ProtocolsPerHandle||!b->OpenProtocolInformation||!b->LocateHandleBuffer||!b->HandleProtocol)return EFI_UNSUPPORTED;
  Plan.console=t->ConsoleOutHandle;Plan.input=t->ConsoleInHandle;Plan.error=t->StandardErrorHandle;
  Plan.text=t->ConOut;Plan.err=t->StdErr;Plan.keys=t->ConIn;
  if(!Plan.console||Plan.console!=Plan.input||Plan.console!=Plan.error||!Plan.text||!Plan.err||!Plan.keys||
     !Plan.keys->ReadKeyStroke||!Plan.keys->Reset||!Plan.keys->WaitForKey)return EFI_UNSUPPORTED;
  Plan.key_reset=(UINTN)Plan.keys->Reset;Plan.key_read=(UINTN)Plan.keys->ReadKeyStroke;Plan.key_wait=Plan.keys->WaitForKey;
  if(!Path(pci,PciPath,sizeof(PciPath))||!GuAd103ColdEntry())return EFI_NOT_READY;
  EFI_STATUS s=Gops(TRUE,FALSE);
  if(s==EFI_SUCCESS)s=Alive();
  if(s==EFI_SUCCESS)s=Bar();
  if(s==EFI_SUCCESS)s=ReadConfig(Plan.config);
  if(s==EFI_SUCCESS)s=Gu.pci->Attributes?Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&Plan.attributes):EFI_UNSUPPORTED;
  UINT64 decode=(Plan.config[1]&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(Plan.config[1]&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);
  if(s==EFI_SUCCESS&&(Plan.attributes&(EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER))!=decode)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Snapshot(&Original);
  if(s==EFI_SUCCESS)s=Consumers();
  GuLog("AD103 preflight: status=%#llx graph=%zu code_profiles=5\n",(unsigned long long)s,(size_t)Original.count);
  if(s==EFI_SUCCESS)GuLog("AD103 endpoints: pci=%#llx gop=%#llx console=%#llx aggregate=%#llx\n",
    (unsigned long long)(UINTN)Plan.pci,(unsigned long long)(UINTN)Plan.gop,
    (unsigned long long)(UINTN)Plan.console,(unsigned long long)(UINTN)Plan.aggregate);
  if(s==EFI_SUCCESS)Plan.ready=TRUE;return s;
}
static BOOLEAN DecodeMask(const UINT32 c[64],UINT64 *mask) {
  *mask=0;UINT32 missing=Plan.config[1]&~c[1]&3;
  if((Plan.config[1]&6)!=2||(c[1]&4))return FALSE;
  for(UINTN i=0;i<64;i++)if((c[i]|(i==1?missing:0))!=Plan.config[i])return FALSE;
  *mask=(missing&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(missing&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);return TRUE;
}
static EFI_STATUS ClaimRestore(VOID) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_PCI_IO_PROTOCOL *io=NULL;
  UINT32 c[64];UINT64 mask=0,attrs=0,supported=0;
  EFI_STATUS s=ReadConfig(c);if(s==EFI_SUCCESS&&!DecodeMask(c,&mask))s=EFI_NOT_READY;
  GuLog("AD103 decode restore intent: status=%#llx IO_MEM_mask=%#llx\n",(unsigned long long)s,(unsigned long long)mask);
  EFI_TPL old=b->RaiseTPL(TPL_NOTIFY);
  if(s==EFI_SUCCESS)s=ReadConfig(c);
  if(s==EFI_SUCCESS&&!DecodeMask(c,&mask))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Snapshot(&Current);
  if(s==EFI_SUCCESS&&!Expected(2,FALSE))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=b->OpenProtocol(Plan.pci,&PciGuid,(VOID **)&io,Plan.self,NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE);
  if(s==EFI_SUCCESS){Plan.claimed=TRUE;Gu.owned=TRUE;if(io!=Gu.pci)s=EFI_COMPROMISED_DATA;}
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
  if(s==EFI_SUCCESS&&attrs!=(Plan.attributes&~mask))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationSupported,0,&supported);
  if(s==EFI_SUCCESS&&(supported&mask)!=mask)s=EFI_UNSUPPORTED;
  if(s==EFI_SUCCESS)s=Snapshot(&Current);
  if(s==EFI_SUCCESS&&!Expected(2,TRUE))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS&&mask)s=io->Attributes(io,EfiPciIoAttributeOperationEnable,mask,NULL);
  b->RestoreTPL(old);
  if(s==EFI_SUCCESS)s=ReadConfig(c);
  if(s==EFI_SUCCESS&&!Equal(c,Plan.config,sizeof(c)))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
  if(s==EFI_SUCCESS&&attrs!=Plan.attributes)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Bar();
  if(s==EFI_SUCCESS&&!GuAd103ColdEntry())s=EFI_NOT_READY;
  GuLog("AD103 exclusive claim/decode result: status=%#llx\n",(unsigned long long)s);return s;
}
EFI_STATUS GuDisplayRelease(VOID) {
  if(!Plan.ready||Plan.mutated||Gu.started||Gu.owned)return EFI_NOT_READY;
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;
  for(UINTN stage=1;stage<=2;stage++) {
    GuLog("AD103 driver detach intent: stage=%zu\n",(size_t)stage);
    EFI_STATUS s=Alive();UINT32 c[64];
    if(s==EFI_SUCCESS)s=ReadConfig(c);
    if(s==EFI_SUCCESS&&!Equal(c,Plan.config,sizeof(c)))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS&&!GuAd103ColdEntry())s=EFI_NOT_READY;
    if(s==EFI_SUCCESS)s=Gops(FALSE,FALSE);
    EFI_TPL old=b->RaiseTPL(TPL_NOTIFY);
    if(s==EFI_SUCCESS)s=Snapshot(&Current);
    if(s==EFI_SUCCESS&&!Expected(stage-1,FALSE))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS) {
      if(stage==1){for(UINTN i=0;i<5&&s==EFI_SUCCESS;i++)s=Driver(i);}
      else {s=Driver(0);if(s==EFI_SUCCESS)s=Driver(1);}
    }
    b->RestoreTPL(old);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    /* Record before the callback: an error may follow a partial driver stop. */
    Plan.mutated=TRUE;
    s=b->DisconnectController(stage==2?Plan.pci:Plan.gop,
      stage==1?Plan.edid_agent:Plan.nv_agent,NULL);
    GuLog("AD103 driver detach return: stage=%zu status=%#llx\n",(size_t)stage,(unsigned long long)s);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    Plan.stage=stage;
    const char *check="snapshot";s=Snapshot(&Current);
    if(s==EFI_SUCCESS){check="graph";if(!Expected(stage,FALSE)){s=EFI_NOT_READY;GraphFailure(stage);}}
    if(s==EFI_SUCCESS){check="console";s=Alive();}
    if(s==EFI_SUCCESS){check="GOP";s=Gops(FALSE,stage==2);}
    if(s!=EFI_SUCCESS) {
      NOTE("AD103 post-detach failure: stage=%zu check=%s status=%#llx graph=%zu original=%zu\n",
        (size_t)stage,check,(unsigned long long)s,(size_t)Current.count,(size_t)Original.count);
      return s;
    }
    NOTE("AD103 post-detach verified: stage=%zu graph=%zu\n",(size_t)stage,(size_t)Current.count);
  }
  EFI_STATUS s=ClaimRestore();
  if(s==EFI_SUCCESS&&!GuDisplayReleased())s=EFI_NOT_READY;
  return s;
}
BOOLEAN GuDisplayReleased(VOID) {
  UINT32 c[64];UINT64 attrs=0;const char *failed=NULL;
  if(!Plan.ready||Plan.stage!=2||!Plan.claimed||!Gu.owned||Gu.fault)failed="state";
  else if(Alive()!=EFI_SUCCESS)failed="console";
  else if(Gops(FALSE,FALSE)!=EFI_SUCCESS)failed="GOP";
  else if(ReadConfig(c)!=EFI_SUCCESS)failed="config-read";
  else if(!ConfigStable(c))failed="config-drift";
  else if(Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attrs)!=EFI_SUCCESS||attrs!=Plan.attributes)failed="attributes";
  else if(Snapshot(&Current)!=EFI_SUCCESS)failed="graph-snapshot";
  else if(!Expected(2,TRUE)){failed="graph";}
  if(failed)NOTE("AD103 release check failed: %s attributes=%#llx expected=%#llx graph=%zu original=%zu fault=%u flrs=%u\n",
    failed,(unsigned long long)attrs,(unsigned long long)Plan.attributes,(size_t)Current.count,(size_t)Original.count,Gu.fault,Gu.flrs);
  return !failed;
}
EFI_STATUS GuDisplayFinish(VOID) {
  if(!GuSafeToReturn()||!Gu.quiesced||Gu.flrs!=1||!GuDisplayReleased()){NOTE("AD103 finish refused before close\n");return EFI_NOT_READY;}
  EFI_STATUS s=Plan.table->BootServices->CloseProtocol(Plan.pci,&PciGuid,Plan.self,NULL);
  if(s!=EFI_SUCCESS){NOTE("AD103 finish close status=%#llx\n",(unsigned long long)s);return s;}
  Plan.claimed=FALSE;Gu.owned=FALSE;
  s=Snapshot(&Current);if(s==EFI_SUCCESS&&!Expected(2,FALSE)){s=EFI_NOT_READY;}
  if(s!=EFI_SUCCESS)NOTE("AD103 finish after close status=%#llx graph=%zu\n",(unsigned long long)s,(size_t)Current.count);
  return s;
}
BOOLEAN GuDisplayMutated(VOID){return Plan.mutated;}
BOOLEAN GuDisplayReadyForWindows(VOID) {
  if(!Plan.ready||Plan.stage!=2||Plan.claimed||Gu.owned||Gu.fault||!Gu.quiesced||Gu.flrs!=1||!GuSafeToReturn())return FALSE;
  UINT32 c[64];
  if(Alive()!=EFI_SUCCESS||Gops(FALSE,FALSE)!=EFI_SUCCESS||ReadConfig(c)!=EFI_SUCCESS||!ConfigStable(c)||
     Snapshot(&Current)!=EFI_SUCCESS||!Expected(2,FALSE))return FALSE;
  /* Fresh status after USB Close and before StartImage; no log callbacks. */
  UINT32 chip=GuRead32(0),cpu=GuRead32(0x110100),hi=GuRead32(0x1fa828);
  UINT32 ss0=GuRead32(0x82381c),ss1=GuRead32(0x823820),readout=GuRead32(0x823818),plm=GuRead32(0x823804);
  return !Gu.fault&&chip==0x193000a1&&(cpu&0x10)&&cpu!=0xffffffff&&
    (cpu&0xffff0000)!=0xbadf0000&&!(hi>>4)&&ss0==0x88888888&&ss1==8&&
    !(readout&0x07ffffff)&&readout!=0xffffffff&&plm==0xffffffff;
}
