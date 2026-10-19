
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import collections, math, struct, zlib

class FormatError(ValueError): pass

class Reader:
    def __init__(self,data,offset=0): self.data=data;self.offset=offset
    def take(self,n):
        if n<0 or self.offset+n>len(self.data):raise FormatError('Read beyond block')
        out=self.data[self.offset:self.offset+n];self.offset+=n;return out
    def unpack(self,fmt):
        v=struct.unpack('<'+fmt,self.take(struct.calcsize('<'+fmt)));return v[0] if len(v)==1 else v
    def string(self):return self.take(self.unpack('I')).decode('utf-8')
    def finish(self):
        if self.offset!=len(self.data):raise FormatError(f'{len(self.data)-self.offset} unparsed bytes')

@dataclass
class Field:
    offset:int
    size:int
    fmt:str
    value:object
    reference:bool=False

@dataclass
class Block:
    index:int
    type:str
    data:bytearray

class NifFile:
    @classmethod
    def read(cls,path):return cls.from_bytes(Path(path).read_bytes())
    @classmethod
    def from_bytes(cls,data):
        self=cls();nl=data.find(b'\n')
        if data[:nl]!=b'Gamebryo File Format, Version 20.2.0.7':raise FormatError('Unsupported header')
        r=Reader(data,nl+1)
        if r.unpack('IBI')!=(0x14020007,1,12):raise FormatError('Unsupported version')
        self.count_offset=r.offset;n,self.bs_version=r.unpack('II')
        if self.bs_version not in (83,100) or not 0<n<200000:raise FormatError('Unsupported BS stream/count')
        for _ in range(3):r.take(r.unpack('B'))
        self.preamble=bytes(data[:r.offset]);nt=r.unpack('H');self.type_names=[r.string() for _ in range(nt)]
        indices=[r.unpack('H') for _ in range(n)]
        if any(i>=nt for i in indices):raise FormatError('Invalid type index')
        sizes=[r.unpack('I') for _ in range(n)]
        ns,self.max_string_length=r.unpack('II');self.strings=[r.string() for _ in range(ns)]
        if max((len(s.encode()) for s in self.strings),default=0)!=self.max_string_length:raise FormatError('Invalid string length header')
        ng=r.unpack('I');self.groups=bytes(r.take(ng*4))
        self.blocks=[Block(i,self.type_names[ti],bytearray(r.take(sz))) for i,(ti,sz) in enumerate(zip(indices,sizes))]
        nr=r.unpack('I');self.roots=[r.unpack('i') for _ in range(nr)];r.finish()
        if not self.roots or any(i<0 or i>=n for i in self.roots):raise FormatError('Invalid roots')
        if self.to_bytes()!=data:raise FormatError('Lossless roundtrip failed')
        return self
    def to_bytes(self):
        pre=bytearray(self.preamble);struct.pack_into('<I',pre,self.count_offset,len(self.blocks))
        names=list(self.type_names)
        for b in self.blocks:
            if b.type not in names:names.append(b.type)
        out=bytes(pre)+struct.pack('<H',len(names))
        for s in names:v=s.encode();out+=struct.pack('<I',len(v))+v
        out+=struct.pack('<'+'H'*len(self.blocks),*(names.index(b.type) for b in self.blocks))
        out+=struct.pack('<'+'I'*len(self.blocks),*(len(b.data) for b in self.blocks))
        out+=struct.pack('<II',len(self.strings),max((len(s.encode()) for s in self.strings),default=0))
        for s in self.strings:v=s.encode();out+=struct.pack('<I',len(v))+v
        out+=struct.pack('<I',len(self.groups)//4)+self.groups+b''.join(bytes(b.data) for b in self.blocks)
        return out+struct.pack('<I',len(self.roots))+struct.pack('<'+'i'*len(self.roots),*self.roots)
    def write(self,path):
        self.validate();p=Path(path);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(self.to_bytes())
    def string(self,i):
        if i==0xffffffff:return ''
        if not 0<=i<len(self.strings):raise FormatError('Invalid string index')
        return self.strings[i]
    def indices(self,type_name):return [b.index for b in self.blocks if b.type==type_name]
    def fields(self,index):
        b=self.blocks[index];t=b.type;r=Reader(b.data);fs={}
        def f(name,fmt,ref=False):
            p=r.offset;v=r.unpack(fmt);fs[name]=Field(p,r.offset-p,fmt,v,ref);return v
        def arr(name,fmt,n,ref=False):
            if not 0<=n<1000000:raise FormatError('Invalid array length')
            p=r.offset;v=tuple(r.unpack(fmt) for _ in range(n));fs[name]=Field(p,r.offset-p,fmt+'[]',v,ref);return v
        def st(name):
            p=r.offset;v=r.string();fs[name]=Field(p,r.offset-p,'string',v);return v
        def net():
            f('name','I');arr('extra_data','i',f('num_extra_data','I'),True);f('controller','i',True)
        def av():
            net();f('flags','I');f('translation','3f');f('rotation','9f');f('scale','f');f('collision','i',True)
        def node():
            av();arr('children','i',f('num_children','I'),True);arr('effects','i',f('num_effects','I'),True)
        def modifier():f('name','I');f('order','I');f('target','i',True);f('active','B')
        def time():
            f('next_controller','i',True);f('flags','H')
            for n in ('frequency','phase','start_time','stop_time'):f(n,'f')
            f('target','i',True)
        def keys(prefix,valuefmt):
            n=f(prefix+'num_keys','I')
            if n:
                interp=f(prefix+'interpolation','I');fmt='f'+valuefmt
                if interp==2:fmt+=valuefmt+valuefmt
                elif interp==3:fmt+='3f'
                elif interp not in (1,5):raise FormatError(f'Unexpected key interpolation {interp}')
                arr(prefix+'keys',fmt,n)
        if t in ('NiNode','BSFadeNode','BSOrderedNode','NiBillboardNode','BSMasterParticleSystem','BSValueNode'):
            node()
            if t=='BSOrderedNode':f('alpha_sort_bound','4f');f('static_bound','B')
            if t=='NiBillboardNode':f('billboard_mode','H')
            if t=='BSMasterParticleSystem':f('max_emitter_objects','H');arr('particle_systems','i',f('num_particle_systems','I'),True)
            if t=='BSValueNode':f('addon_node_index','I');f('value_flags','B')
        elif t=='NiParticleSystem':
            av()
            if self.bs_version==100:f('bound','4f');f('skin','i',True)
            else:
                f('data','i',True);f('skin','i',True);n=f('num_materials','I');arr('material_names','I',n);arr('material_extra_data','i',n);f('active_material','i');f('material_needs_update','B')
            f('shader','i',True);f('alpha','i',True)
            if self.bs_version==100:f('vertex_desc','Q')
            f('near_far','4H')
            if self.bs_version==100:f('data','i',True)
            f('world_space','B');arr('modifiers','i',f('num_modifiers','I'),True)
        elif t=='BSEffectShaderProperty':
            net();f('shader_flags_1','I');f('shader_flags_2','I');f('uv_offset','2f');f('uv_scale','2f');st('source_texture')
            for n in ('clamp_mode','lighting_influence','env_map_min_lod','unused_byte'):f(n,'B')
            for n in ('falloff_start_angle','falloff_stop_angle','falloff_start_opacity','falloff_stop_opacity'):f(n,'f')
            f('base_color','4f');f('base_color_scale','f');f('soft_falloff_depth','f');st('greyscale_texture')
        elif t=='BSLightingShaderProperty':
            if self.bs_version!=100:raise NotImplementedError(t)
            shader_type=f('shader_type','I')
            if shader_type!=0:raise NotImplementedError(t+' non-default shader')
            net();f('shader_flags_1','I');f('shader_flags_2','I')
            f('uv_offset','2f');f('uv_scale','2f');f('texture_set','i',True)
            f('emissive_color','3f');f('emissive_multiple','f');f('clamp_mode','I')
            f('alpha','f');f('refraction_strength','f');f('glossiness','f')
            f('specular_color','3f');f('specular_strength','f')
            f('lighting_effect_1','f');f('lighting_effect_2','f')
        elif t=='BSShaderTextureSet':
            count=f('num_textures','I')
            if count>16:raise FormatError('Unexpected shader texture count')
            for j in range(count):st(f'texture_{j}')
        elif t=='NiAlphaProperty':net();f('flags','H');f('threshold','B')
        elif t in ('NiPSysCylinderEmitter','NiPSysBoxEmitter','NiPSysSphereEmitter','NiPSysMeshEmitter'):
            modifier()
            for n in ('speed','speed_variation','declination','declination_variation','planar_angle','planar_angle_variation'):f(n,'f')
            f('initial_color','4f')
            for n in ('initial_radius','radius_variation','lifespan','lifespan_variation'):f(n,'f')
            if t=='NiPSysMeshEmitter':
                arr('emitter_meshes','i',f('num_emitter_meshes','I'),True);f('initial_velocity_type','I');f('emission_type','I');f('emission_axis','3f')
            else:
                f('emitter_object','i',True)
                for n in {'NiPSysCylinderEmitter':('volume_radius','volume_height'),'NiPSysBoxEmitter':('volume_width','volume_height','volume_depth'),'NiPSysSphereEmitter':('volume_radius',)}[t]:f(n,'f')
        elif t=='BSPSysSimpleColorModifier':
            modifier()
            for n in ('fade_in_percent','fade_out_percent','color_1_end_percent','color_1_start_percent','color_2_end_percent','color_2_start_percent'):f(n,'f')
            arr('colors','4f',3)
        elif t=='BSPSysScaleModifier':modifier();arr('scales','f',f('num_scales','I'))
        elif t=='BSPSysSubTexModifier':
            modifier()
            for n in ('start_frame','start_frame_fudge','end_frame','loop_start_frame','loop_start_frame_fudge','frame_count','frame_count_fudge'):f(n,'f')
        elif t=='NiPSysGravityModifier':
            modifier();f('gravity_object','i',True);f('gravity_axis','3f');f('decay','f');f('strength','f');f('force_type','I');f('turbulence','f');f('turbulence_scale','f');f('world_aligned','B')
        elif t=='NiPSysDragModifier':modifier();f('drag_object','i',True);f('drag_axis','3f');f('percentage','f');f('range','f');f('range_falloff','f')
        elif t=='NiPSysRotationModifier':
            modifier()
            for n in ('rotation_speed','rotation_speed_variation','rotation_angle','rotation_angle_variation'):f(n,'f')
            f('random_speed_sign','B');f('random_axis','B');f('axis','3f')
        elif t=='NiPSysSpawnModifier':
            modifier();f('num_generations','H');f('percentage_spawned','f');f('min_spawn','H');f('max_spawn','H')
            for n in ('speed_variation','direction_variation','lifespan','lifespan_variation'):f(n,'f')
        elif t=='NiPSysAgeDeathModifier':modifier();f('spawn_on_death','B');f('spawn_modifier','i',True)
        elif t=='NiPSysPositionModifier':modifier()
        elif t=='NiPSysBoundUpdateModifier':modifier();f('update_skip','H')
        elif t in ('NiPSysEmitterCtlr','BSPSysMultiTargetEmitterCtlr','NiPSysModifierActiveCtlr','NiPSysEmitterSpeedCtlr','NiPSysEmitterDeclinationVarCtlr','NiPSysEmitterPlanarAngleVarCtlr','NiPSysEmitterInitialRadiusCtlr','NiPSysEmitterLifeSpanCtlr','NiPSysUpdateCtlr'):
            time()
            if t!='NiPSysUpdateCtlr':
                f('interpolator','i',True);f('modifier_name','I')
                if t in ('NiPSysEmitterCtlr','BSPSysMultiTargetEmitterCtlr'):f('visibility_interpolator','i',True)
                if t=='BSPSysMultiTargetEmitterCtlr':f('max_emitters','H');f('master_particle_system','i',True)
        elif t in ('BSEffectShaderPropertyColorController','BSEffectShaderPropertyFloatController','BSLightingShaderPropertyFloatController'):
            time();f('interpolator','i',True);f('controlled_color' if t.endswith('ColorController') else 'controlled_variable','I')
        elif t=='NiTransformController':time();f('interpolator','i',True)
        elif t=='NiControllerManager':
            time();f('cumulative','B');arr('controller_sequences','i',f('num_controller_sequences','I'),True);f('object_palette','i',True)
        elif t=='NiMultiTargetTransformController':time();arr('extra_targets','i',f('num_extra_targets','H'),True)
        elif t=='NiControllerSequence':
            f('name','I');n=f('num_controlled_blocks','I');f('array_grow_by','I')
            for j in range(n):
                prefix=f'controlled_{j}_';f(prefix+'interpolator','i',True);f(prefix+'controller','i',True);f(prefix+'priority','B')
                for key in ('node_name','property_type','controller_type','controller_id','interpolator_id'):f(prefix+key,'I')
            f('weight','f');f('text_keys','i',True);f('cycle_type','I');f('frequency','f');f('start_time','f');f('stop_time','f');f('manager','i',True);f('accum_root_name','I');arr('anim_notes','i',f('num_anim_notes','H'),True)
        elif t in ('NiFloatInterpolator','NiBoolInterpolator','NiBoolTimelineInterpolator','NiPoint3Interpolator'):
            f('value',{'NiFloatInterpolator':'f','NiPoint3Interpolator':'3f'}.get(t,'B'));f('data','i',True)
        elif t in ('NiFloatData','NiBoolData','NiPosData'):keys('',{'NiFloatData':'f','NiBoolData':'B','NiPosData':'3f'}[t])
        elif t=='NiTransformInterpolator':f('translation','3f');f('rotation','4f');f('scale','f');f('data','i',True)
        elif t=='NiTransformData':
            n=f('num_rotation_keys','I')
            if n:
                it=f('rotation_interpolation','I')
                if it==4:
                    for axis in 'xyz':keys('rotation_'+axis+'_','f')
                elif it in (1,2,3):arr('rotation_keys','5f'+('3f' if it==3 else ''),n)
                else:raise FormatError('Unsupported rotation keys')
            keys('translation_','3f');keys('scale_','f')
        elif t=='NiPSysData':
            f('group_id','i');f('max_particles','H');f('keep_flags','B');f('compress_flags','B');f('has_vertices','B');f('data_flags','H');f('material_crc','I');f('has_normals','B');f('bound','4f');f('has_colors','B');f('consistency_flags','H');f('additional_data','i',True);f('has_radii','B');f('num_active','H')
            for n in ('has_sizes','has_rotations','has_rotation_angles','has_rotation_axes','has_texture_indices'):f(n,'B')
            arr('subtextures','4f',f('num_subtextures','I'));f('aspect','f');f('aspect_flags','H');f('speed_aspect','3f');f('has_rotation_speeds','B')
        else:raise NotImplementedError(t)
        try:r.finish()
        except FormatError as e:raise FormatError(f'Block {index} {t}: {e}') from e
        return fs
    def values(self,index):return {k:f.value for k,f in self.fields(index).items()}
    def set(self,index,**changes):
        fs=self.fields(index);edits=[]
        for key,val in changes.items():
            if key not in fs:raise FormatError(f'Unknown field {key}')
            field=fs[key]
            if field.fmt=='string':raw=val.encode();raw=struct.pack('<I',len(raw))+raw
            elif field.fmt.endswith('[]'):
                fmt=field.fmt[:-2];raw=b''.join(struct.pack('<'+fmt,*(v if isinstance(v,(tuple,list)) else (v,))) for v in val)
                if len(raw)!=field.size:raise FormatError('Variable arrays require explicit setter')
            else:raw=struct.pack('<'+field.fmt,*(val if isinstance(val,(tuple,list)) else (val,)))
            edits.append((field.offset,field.size,raw))
        for off,size,raw in sorted(edits,reverse=True):self.blocks[index].data[off:off+size]=raw
        self.fields(index)
    def set_scales(self,index,values):
        fs=self.fields(index);o=fs['num_scales'].offset;self.blocks[index].data[o:]=struct.pack('<I',len(values))+struct.pack('<'+'f'*len(values),*values);self.fields(index)
    def set_keys(self,index,keys,interpolation=1):
        t=self.blocks[index].type
        if t not in ('NiFloatData','NiBoolData','NiPosData') or interpolation not in (1,5):raise FormatError('Unsupported key write')
        fmt={'NiFloatData':'ff','NiBoolData':'fB','NiPosData':'4f'}[t]
        self.blocks[index].data=bytearray(struct.pack('<II',len(keys),interpolation)+b''.join(struct.pack('<'+fmt,*k) for k in keys));self.fields(index)
    def set_transform_keys(self,index,translations=None,scales=None):
        if self.blocks[index].type!='NiTransformData':raise FormatError('Expected transform data')
        for prefix,items,fmt in [('scale_',scales,'ff'),('translation_',translations,'4f')]:
            if items is None:continue
            fs=self.fields(index);start=fs[prefix+'num_keys'].offset
            if fs[prefix+'num_keys'].value:end=fs[prefix+'keys'].offset+fs[prefix+'keys'].size
            else:end=start+4
            raw=struct.pack('<I',len(items))
            if items:raw+=struct.pack('<I',1)+b''.join(struct.pack('<'+fmt,*k) for k in items)
            self.blocks[index].data[start:end]=raw;self.fields(index)
    def set_subtextures(self,index,rectangles=((0.,1.,0.,1.),),aspect=1.,aspect_flags=0):
        fs=self.fields(index);a=fs['num_subtextures'].offset;end=fs['subtextures'].offset+fs['subtextures'].size
        self.blocks[index].data[a:end]=struct.pack('<I',len(rectangles))+b''.join(struct.pack('<4f',*x) for x in rectangles)
        self.set(index,aspect=aspect,aspect_flags=aspect_flags)
    def replace_embedded_paths(self,mapping):
        norm=lambda s:s.replace('/','\\').casefold();lookup={norm(k):v for k,v in mapping.items()};count=0
        for i in self.indices('BSEffectShaderProperty'):
            v=self.values(i);changes={k:lookup[norm(v[k])] for k in ('source_texture','greyscale_texture') if norm(v[k]) in lookup}
            if changes:self.set(i,**changes);count+=len(changes)
        return count
    def texture_paths(self):
        result={v[k] for i in self.indices('BSEffectShaderProperty') for v in [self.values(i)] for k in ('source_texture','greyscale_texture') if v[k]}
        for i in self.indices('BSShaderTextureSet'):
            v=self.values(i)
            result.update(v[f'texture_{j}'] for j in range(v['num_textures']) if v[f'texture_{j}'])
        return sorted(result)
    def controlled_blocks(self,index):

        if self.blocks[index].type!='NiControllerSequence':raise FormatError('Expected controller sequence')
        v=self.values(index);out=[]
        for j in range(v['num_controlled_blocks']):
            prefix=f'controlled_{j}_';item={k[len(prefix):]:val for k,val in v.items() if k.startswith(prefix)}
            for key in ('node_name','property_type','controller_type','controller_id','interpolator_id'):item[key]=self.string(item[key])
            out.append(item)
        return out
    def validate(self):
        refs=0;known=0
        for b in self.blocks:
            try:fields=self.fields(b.index)
            except NotImplementedError:continue
            known+=1
            for name,f in fields.items():
                if f.reference:
                    vals=f.value if f.fmt.endswith('[]') else (f.value,)
                    for v in vals:
                        if v!=-1 and not 0<=v<len(self.blocks):raise FormatError(f'Invalid reference {b.index}:{name}={v}')
                        refs+=1
                if name in ('name','modifier_name'):self.string(f.value)
                vals=f.value if isinstance(f.value,(tuple,list)) else (f.value,)
                for val in vals:
                    if isinstance(val,float) and not math.isfinite(val):raise FormatError(f'Nonfinite field {b.index}:{name}')
        check=NifFile.from_bytes(self.to_bytes())
        if check.to_bytes()!=self.to_bytes():raise FormatError('Output roundtrip failed')
        return {'blocks':len(self.blocks),'typed_blocks_validated':known,'references_validated':refs,'lossless_roundtrip':True}
    def summary(self):return {'bs_version':self.bs_version,'block_types':dict(collections.Counter(b.type for b in self.blocks)),'textures':self.texture_paths(),'validation':self.validate()}

def write_dds_rgba(path,width,height,pixels):

    if width<1 or height<1 or width&(width-1) or height&(height-1) or len(pixels)!=width*height*4:raise ValueError('Power-of-two dimensions / RGBA length required')
    levels=[bytes(pixels)];w,h=width,height
    while w>1 or h>1:
        nw,nh=max(1,w//2),max(1,h//2);dst=bytearray();src=levels[-1]
        for y in range(nh):
            for x in range(nw):
                offsets=[(min(h-1,y*2+dy)*w+min(w-1,x*2+dx))*4 for dy in range(2) for dx in range(2)]
                samples=[src[offset:offset+4] for offset in offsets]
                sa=sum(p[3] for p in samples)
                rgb=[round(sum(p[c]*p[3] for p in samples)/sa) if sa else 255 for c in range(3)]
                dst.extend((*rgb,round(sa/4)))
        levels.append(bytes(dst));w,h=nw,nh
    header=[124,0x2100f,height,width,width*4,0,len(levels)]+[0]*11
    header += [32,0x41,0,32,0xff0000,0xff00,0xff,0xff000000,0x401008,0,0,0,0]
    data=bytearray(b'DDS '+struct.pack('<31I',*header))
    for level in levels:
        for i in range(0,len(level),4):r,g,b,a=level[i:i+4];data.extend((b,g,r,a))
    p=Path(path);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    return {'path':str(p),'width':width,'height':height,'mip_count':len(levels),'bytes':len(data),'format':'BGRA8_UNORM'}

def write_png_rgba(path,width,height,pixels):
    def chunk(kind,payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload))
    scan=b''.join(b'\0'+pixels[y*width*4:(y+1)*width*4] for y in range(height))
    p=Path(path);p.parent.mkdir(parents=True,exist_ok=True)
    p.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(scan,9))+chunk(b'IEND',b''))
