// Made by tools/visualiser/extract.py from the mockup. Do not edit by hand.
namespace VisShaders
{
static const char* const VS = R"GLSL(#version 330 core
layout(location=0) in vec2 p;out vec2 uv;void main(){uv=p*.5+.5;gl_Position=vec4(p,0,1);})GLSL";
static const char* const INK_STEP = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D prev;uniform float dt,swirl,flow,fade,scale,detail,chaos,comp,swl,cf;uniform vec2 ctr;uniform vec3 vx[6];uniform int nv;uniform vec4 sp[8];uniform vec3 sc[8];uniform int ns;
void main(){vec2 q=vec2(uv.x*asp,uv.y);vec2 s=q*scale+vec2(ph*.06,-ph*.045);s+=chaos*1.7*(vec2(fbm(s*1.6+vec2(ph*.21,0.)),fbm(s*1.6+vec2(5.2,-ph*.17)))-.5);float e=.015;
 vec2 v=vec2(fbm(s+vec2(0,e))-fbm(s-vec2(0,e)),-(fbm(s+vec2(e,0))-fbm(s-vec2(e,0))))/(2.*e);
 vec2 s2=q*scale*3.4+vec2(-ph*.1,ph*.08)+chaos*vec2(sin(ph*.7),cos(ph*.6));vec2 v2=vec2(fbm(s2+vec2(0,e))-fbm(s2-vec2(0,e)),-(fbm(s2+vec2(e,0))-fbm(s2-vec2(e,0))))/(2.*e);
 vec2 d=q-vec2(asp*.5,.5);v=v*swirl+v2*swirl*(detail*.9+high*.3)+vec2(-d.y,d.x)*flow*exp(-dot(d,d)*2.5);
 vec2 dc=q-ctr;float rc=dot(dc,dc);v+=vec2(-dc.y,dc.x)*swl*(.35+exp(-rc*3.))+dc*cf*exp(-rc*1.5);
 for(int i=0;i<6;i++){if(i>=nv)break;vec2 dv=q-vx[i].xy;v+=vec2(-dv.y,dv.x)*vx[i].z*exp(-dot(dv,dv)*18.)*6.;}
 vec3 c=texture(prev,uv-v*dt*vec2(1./asp,1.)*.11).rgb*fade;
 for(int i=0;i<8;i++){if(i>=ns)break;vec2 dd=q-sp[i].xy;c+=sc[i]*sp[i].w*exp(-dot(dd,dd)/(sp[i].z*sp[i].z));}
 float mx=max(c.r,max(c.g,c.b));c*=1.-comp*(1.-c/max(mx,1e-4));
 o=vec4(min(c,vec3(2.2)),1.);})GLSL";
static const char* const INK_SHOW = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D dye;uniform vec2 px;uniform int mir;uniform float sharp,mono,rot,psy,trip,tw,segs,lsp,marble,bleed,breathe;uniform int form,surf;
vec2 tri(vec2 s){return 1.-abs(1.-mod(s,2.));}
vec2 fold(vec2 u,float k){float on=step(.001,lsp);vec2 d=(u-.5)*vec2(asp,1.)*(1.-pz)*(1.+k*lsp)*(1.+breathe*.16*sin(ph*.9));float ang=rot*(1.-k*.3*on)+k*1.1*on+tw/(1.+dot(d,d)*5.);float c=cos(ang),s=sin(ang);d=mat2(c,-s,s,c)*d;d=kal(d,mir);
 if(trip>0.){d+=trip*.05*sin(d.yx*7.+t*.8)*(1.+low*.6);for(int i=0;i<4;i++){float w=clamp(trip*4.-float(i),0.,1.);if(w<=0.)break;
  float a=ph*.04+float(i)*1.3,ca=cos(a),sa=sin(a);vec2 q=mat2(ca,-sa,sa,ca)*(abs(d)*1.55-vec2(.5,.36));d=mix(d,q,w*w*(3.-2.*w));}}
 vec2 r;
 if(form==1){float sg=6.2831853/segs,a=abs(mod(atan(d.y,d.x),sg)-sg*.5),rr=length(d);r=rr*vec2(cos(a),sin(a))*2./vec2(asp,1.);}
 else if(form==2){float a=atan(d.y,d.x)/6.2831853+.5;r=vec2(abs(fract(a*segs)-.5)*2.,length(d)*1.7);}
 else if(form==3){r=d*segs*.5/vec2(asp,1.);}
 else if(mir>=2)r=d*2./vec2(asp,1.);else if(mir==1)r=vec2(d.x*2./asp,d.y+.5);else r=d/vec2(asp,1.)+.5;
 return tri(r+k*vec2(.37,.23)*on);}
vec3 dyeAt(vec2 u,vec2 b){return b.x>0.?(texture(dye,u+b).rgb+texture(dye,u-b).rgb+texture(dye,u+vec2(b.x,-b.y)).rgb+texture(dye,u+vec2(-b.x,b.y)).rgb)*.25:texture(dye,u).rgb;}
float lum(vec2 u){vec3 c=texture(dye,u).rgb;return (c.r+c.g+c.b)*.42;}
/* one layer of ink: colour in rgb, how much it covers in a */
vec4 shade(vec2 uvp,float k){vec2 u=fold(uvp,k);vec2 pk=px*(1.+k*1.6);vec3 c=dyeAt(u,k>0.?pk*1.3:vec2(0.));if(bleed>0.){vec2 b=px*bleed*7.;vec3 s=c;for(int i=0;i<8;i++){float a=float(i)*.7854;s+=texture(dye,u+vec2(cos(a),sin(a))*b*(i%2==0?1.:.55)).rgb;}c=mix(c,max(c,s/9.*1.25),.85);}
 float hx=lum(u+vec2(pk.x,0))-lum(u-vec2(pk.x,0)),hy=lum(u+vec2(0,pk.y))-lum(u-vec2(0,pk.y));
 float ns=surf==1?10.:3.5;vec3 n=normalize(vec3(-hx*ns,-hy*ns,1.));vec3 ld=normalize(vec3(-.4,.55,.75));
 float dif=max(dot(n,ld),0.),spec=pow(max(dot(reflect(-ld,n),vec3(0,0,1)),0.),surf==1?42.:18.);
 float ex=.7+flash*1.1+lvl*.35;float lm=c.r+c.g+c.b;vec3 wt=pow(c/max(lm,1e-4),vec3(sharp));wt/=max(wt.x+wt.y+wt.z,1e-4);
 vec3 hue=wt.x*c1+wt.y*c2+wt.z*c3;hue=mix(hue,c2,mono*.6*smoothstep(.6,1.9,lm));hue/=max(max(hue.r,hue.g),max(hue.b,.25));
 float edge=length(vec2(hx,hy));vec3 col;float cov;
 if(surf==2){float lv=lm*1.7;float f=abs(fract(lv)-.5),fw=max(fwidth(lv),1e-4);float ln=smoothstep(fw*1.7,0.,f)+exp(-f/(fw*5.+.03))*.35*glow;
  float soft=1.-exp(-lm*ex);col=hue*ln*(.75+flash*.9)+hue*soft*.06;cov=min(1.,ln*.8+soft*.25);}
 else if(surf==1){float dens=pow(1.-exp(-lm*ex*2.4),.8);col=hue*dens*(.3+.8*dif)+vec3(1.)*spec*(.55+flash*.8)*smoothstep(.0,.04,edge+lum(u)*.05);cov=dens;}
 else{float dens=pow(1.-exp(-lm*ex),1.35);col=hue*dens*.9*(.62+.5*dif);
  col+=mix(hue,vec3(1.),.3)*spec*(.1+flash*.7)*smoothstep(.0,.06,edge+lum(u)*.05)*glow;cov=dens;}
 if(marble>0.){float vein=abs(fract(lm*(2.5+marble*5.)+fbm(u*vec2(asp,1.)*7.)*1.6)-.5)*2.;col=col*mix(1.,.3+.7*smoothstep(0.,.5,vein),marble)+hue*marble*.5*smoothstep(.16,0.,vein)*cov;}
 float ca=edge*(.6+high*2.5+flash*2.);col+=mix(c1,c2,.5+.5*sin(t*.7))*ca*.3;
 return vec4(max(col,0.),clamp(cov,0.,1.));}
void main(){vec4 a=shade(uv,0.);vec3 col=a.rgb;float cov=a.a;
 if(lsp>0.){for(int k=1;k<3;k++){vec4 b=shade(uv,float(k));col+=b.rgb*(1.-cov)*(k==1?.7:.45);cov+=(1.-cov)*b.a;}}
 vec2 cc=uv-.5;col+=c1*flash*.2*exp(-dot(cc*vec2(asp,1.),cc*vec2(asp,1.))*9.);
 if(psy>0.){float band=cov*2.6+dot(col,vec3(.6))+length(cc*vec2(asp,1.))*3.5-t*.7+low*1.5;col=max(hrot(col,psy*(2.2*sin(band)+1.4*sin(band*.37+t*.3)+flash*2.)),0.);}
 o=vec4(max(col,0.),1.);})GLSL";
static const char* const TRAIL = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D cur,trail;uniform float tdec,tzoom,psy,cap,trot;
void main(){vec2 d=(uv-.5)*vec2(asp,1.);float a=trot*(.45+.75/(1.+dot(d,d)*5.));float ca=cos(a),sa=sin(a);d=mat2(ca,-sa,sa,ca)*d*tzoom;vec3 pv=texture(trail,d/vec2(asp,1.)+.5).rgb;if(psy>0.)pv=max(hrot(pv,psy*.22),0.);pv=pv*tdec-.003;
 o=vec4(max(texture(cur,uv).rgb*cap,pv),1.);})GLSL";
static const char* const COPY = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D tex,cur;void main(){vec2 cc=uv-.5;o=vec4(max(texture(tex,uv).rgb,texture(cur,uv).rgb)*(1.-.55*dot(cc,cc)),1.);})GLSL";
static const char* const LIQ = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform vec3 bl[16];uniform int nb,surf,form,mir;uniform float rot,psy,stirv,wob,lay,TH,shim;
vec2 fld(vec2 p,out vec2 ga,out vec2 gb){float fa=0.,fb=0.;ga=vec2(0.);gb=vec2(0.);for(int i=0;i<16;i++){if(i>=nb)break;vec2 d=p-bl[i].xy;float q=dot(d,d)+1e-3,r2=bl[i].z*bl[i].z,v=r2/q;vec2 g=-2.*r2*d/(q*q);if(i%2==0){fa+=v;ga+=g;}else{fb+=v;gb+=g;}}return min(vec2(fa,fb),vec2(14.));}
/* one colour of blob in the chosen look: lamp (glowing oil), glass (clear, with a rim and a highlight) or neon (outlines only) */
float PX,PA;float edge(float f,vec2 g){float w=max(.07,length(g)*PA);return smoothstep(TH-w,TH+w,f);}
vec3 look(float f,vec2 gf,vec3 ca,vec3 cb,vec3 cr,float cell){float gl=max(length(gf)*PX,1e-4),m=edge(f,gf),nr=smoothstep(3.5,1.6,f),e=abs(f-TH)/gl;
 if(surf==1){float h=sqrt(max(1.-1./max(f,1.),0.));vec2 g=gf/(2.*max(h,.06)*f*f)*smoothstep(11.,4.,f);vec3 n=normalize(vec3(-g*.2,1.));
  float sp=pow(max(dot(n,normalize(vec3(-.45,.6,.66))),0.),22.),fr=pow(1.-n.z,1.5);
  return ((ca*.09+cb*.10*smoothstep(1.,5.,f))*cell+cr*fr*.75+mix(cr,vec3(1.),.65)*sp*.9)*m+cr*exp(-e*.1)*.35*nr;}
 if(surf==2){float e2=abs(f-2.3)/gl;return cr*(exp(-e*.3)*1.15+exp(-e*.05)*.3*glow)*nr+cb*exp(-e2*.3)*.5*m*smoothstep(6.,3.5,f)+ca*.05*m;}
 return (mix(ca,cb,smoothstep(1.,4.,f))*(.5+.3*smoothstep(1.,7.,f))*cell+cr*exp(-e*.09)*.55*nr)*m;}
void main(){vec2 p=(uv-.5)*vec2(asp,1.)*2.*(1.-pz*.6);float ca=cos(rot),sa=sin(rot);p=mat2(ca,-sa,sa,ca)*p;if(form==3)p=abs(p);p=kal(p,mir);
 float wb=wob*2.;p+=(.09*(vec2(fbm(p*1.5+ph*.15),fbm(p*1.5-ph*.13+7.))-.5)*(1.+stirv*2.)+.022*(vec2(fbm(p*5.+ph*.3),fbm(p*5.-ph*.27+3.))-.5))*wb;
 /* edge widths are worked out from the shape itself, not from neighbouring pixels: smooth at any size */
 PX=2.4*(1.-pz*.6)/900.;PA=2.*(1.-pz*.6)*fwidth(uv.y);
 vec2 ga,gb;vec2 F2=fld(p,ga,gb);float fa=F2.x,fb=F2.y;
 float ma=edge(fa,ga),mb=edge(fb,gb);
 float cell=.8+.4*fbm(p*4.+vec2(ph*.2,-ph*.17));
 vec3 col=c1*(.05+.05*lvl)*(1.3-length(p)*.45)+c1*(surf==0?.10:.04)*smoothstep(.25,1.,fa+fb);
 if(lay>.01){vec2 p2=mat2(.8,-.6,.6,.8)*p*1.45+vec2(sin(ph*.05),cos(ph*.04))*.35;vec2 g1,g2;vec2 G2=fld(p2,g1,g2);float f2=G2.x+G2.y,m2=smoothstep(.9,1.15,f2);
  col+=((c1*.13+c3*.05)*m2+c2*exp(-abs(f2-1.)/max(length(g1+g2)*PX*1.45,1e-4)*.12)*.16*smoothstep(3.5,1.6,f2))*lay*1.6;}
 vec3 A=look(fa,ga,c1,c2,c2,cell),B=look(fb,gb,c3,c1,c3,cell);
 col=col*(1.-.75*max(ma,mb))+A*(1.-.4*mb)+B*(1.-.45*ma)+(surf==0?c2*ma*mb*.12:vec3(0.));
 /* shimmer is caustics: a web of light that ripples through the oil and bends with it, brighter on highs */
 if(shim>0.){vec2 q=p*11.+vec2(fa,fb)*.7;float tt=ph*.22;
  float d1=abs(vn(q+vec2(tt,-tt*.7))-vn(q*1.31+vec2(-tt*.6,tt*.9)+5.)),d2=abs(vn(q*.83+vec2(tt*.5,tt*.4)+11.)-vn(q*1.07-vec2(tt*.7,tt*.3)+2.));
  col+=mix(mix(c2,c3,mb/(ma+mb+1e-4)),vec3(1.),.3)*(exp(-d1*d1*420.)+exp(-d2*d2*420.)*.7+exp(-min(d1,d2)*11.)*.22)*shim*.55*max(ma,mb)*(.6+high*1.6);}
 if(psy>0.)col=max(hrot(col,psy*(min(fa+fb,4.5)*.7+sin(t*.5)*1.5)),0.);
 col*=(.85+.3*lvl)*(1.+flash*.7);
 o=vec4(col,1.);})GLSL";
static const char* const SAND = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform vec4 md;uniform float bl,rot,psy,grain,shake,sharp,dnk,drift;uniform int surf,form,mir;
float chl(vec2 q,vec2 nm){return cos(nm.x*q.x)*cos(nm.y*q.y)-cos(nm.y*q.x)*cos(nm.x*q.y);}
/* the figure the grains settle on: a square plate, a round plate, or ripples crossing from three points */
float fig(vec2 g,vec2 nm){
 if(form==1){float r=length(g),th=atan(g.y,g.x);return cos(nm.x*th)*sin(nm.y*r*2.6+.6)+.3*cos((nm.x+nm.y)*r*1.7);}
 if(form==2){float d1=length(g-vec2(.6,0.)),d2=length(g+vec2(.6,0.)),d3=length(g-vec2(0.,.55));return .5*(cos(nm.x*4.2*d1)+cos(nm.y*4.2*d2))+.3*cos((nm.x+nm.y)*2.*d3);}
 return chl(g*1.5708,nm);}
void main(){vec2 p=(uv-.5)*vec2(asp,1.)*2.*(1.-pz*.5);float ca=cos(rot),sa=sin(rot);p=mat2(ca,-sa,sa,ca)*p;p=kal(p,mir);
 float G=(70.+grain*190.)*(surf==1?2.2:surf==2?.4:1.);vec2 cell=floor(p*G),f=fract(p*G)-.5;float h=h21(cell),h2=h21(cell+17.3);
 vec2 jit=surf==2?vec2(0.):(vec2(h2,h21(cell+5.1))-.5)*.7;vec2 gp=(cell+.5+jit)/G;
 gp+=shake*.07*(vec2(vn(cell*.25+t*11.),vn(cell*.25-t*9.+3.))-.5);
 gp+=drift*.16*vec2(sin(t*.33+gp.y*3.1),cos(t*.29+gp.x*2.7));
 float s=mix(fig(gp,md.xy),fig(gp,md.zw),bl);
 float w=(.07+shake*.3+bl*(1.-bl)*.5)*(1.75-sharp*1.5);float dens=exp(-abs(s)/w)*dnk;
 vec3 col;
 if(surf==2){float lit=smoothstep(.3,.8,dens+(h-.5)*.25),d=length(f);
  col=pal(h*.6+s*.6)*(smoothstep(.42,.33,d)*(.035+1.25*lit)+exp(-d*d*9.)*lit*.35*glow)+vec3(1.)*smoothstep(.2,0.,d)*lit*.3+c1*.03;}
 else{float on=step(h,dens*.96+.015);float d=length(f-jit);float dt_=smoothstep(.36,.1,d)*on;
  col=pal(h*2.+s*.6)*dt_*(.75+1.1*dens+high*1.4*step(.9,h2))+c1*.03+c1*dens*dens*(surf==1?.3:.12);}
 if(psy>0.)col=max(hrot(col,psy*(s*3.+length(p)*2.-t*.5)),0.);
 col*=(.85+.3*lvl)*(1.+flash*.8);
 o=vec4(col,1.);})GLSL";
static const char* const BL_STEP = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D prev;uniform vec2 px;uniform float fd,kl,clr,up,dn;uniform vec4 sp[8];uniform int ns;
void main(){vec4 c=texture(prev,uv),n1=texture(prev,uv+vec2(px.x,0.)),n2=texture(prev,uv-vec2(px.x,0.)),n3=texture(prev,uv+vec2(0.,px.y)),n4=texture(prev,uv-vec2(0.,px.y));
 vec2 l=-c.rg+.2*(n1.rg+n2.rg+n3.rg+n4.rg)
  +.05*(texture(prev,uv+px).rg+texture(prev,uv-px).rg+texture(prev,uv+vec2(px.x,-px.y)).rg+texture(prev,uv+vec2(-px.x,px.y)).rg);
 float a=1.-c.r,b=c.g,r=a*b*b,lf=c.b,rf=c.a;
 a+=-l.r-r+fd*(1.-a);b+=.5*l.g+r-(kl+fd)*b;
 /* every patch of growth has a life: it forms, glows, dims and dissolves, then the ground rests before anything can grow there again */
 float av=.25*(n1.b+n2.b+n3.b+n4.b);
 if(rf>0.){b*=.88;lf=1.;rf-=dn;if(rf<=0.){rf=0.;lf=0.;}}
 else if(b>.1){lf=mix(lf,max(lf,av),.1)+up;if(lf>=1.){rf=1.;lf=1.;}}
 else lf=min(mix(lf,av,.6)*.985,.98);
 if(clr>.5){a=1.;b=0.;lf=0.;rf=0.;}
 for(int i=0;i<8;i++){if(i>=ns)break;float d=length((uv-sp[i].xy)*vec2(asp,1.));if(d<sp[i].z){float k=smoothstep(sp[i].z,sp[i].z*.45,d);if(sp[i].w>0.){lf*=1.-k;rf*=1.-k;b=max(b,.9*k);a=min(a,1.-.5*k);}else{lf=0.;rf=0.;b=0.;a=1.;}}}
 o=vec4(1.-clamp(a,0.,1.),clamp(b,0.,1.),lf,rf);})GLSL";
static const char* const BL_SHOW = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D dye;uniform vec2 px;uniform float rot,psy,fresh,halo,ringk;uniform float ons[6];uniform int surf,mir;
void main(){vec2 q=(uv-.5)*vec2(asp,1.)*(1.-pz*.5);float ca=cos(rot),sa=sin(rot);q=mat2(ca,-sa,sa,ca)*q;float rq=length(q)*2.;q=kal(q,mir);vec2 u=q/vec2(asp,1.)+.5;
 vec2 hp=px*.42;vec4 dd=.25*(texture(dye,u+hp)+texture(dye,u-hp)+texture(dye,u+vec2(hp.x,-hp.y))+texture(dye,u+vec2(-hp.x,hp.y)));float b=dd.g,dy=1.-smoothstep(0.,.25,dd.a),fr=pow(1.-dd.b,1.7)*dy,nw=smoothstep(.22,0.,dd.b)*dy;float bx=texture(dye,u+vec2(px.x,0.)).g-texture(dye,u-vec2(px.x,0.)).g,by=texture(dye,u+vec2(0.,px.y)).g-texture(dye,u-vec2(0.,px.y)).g;
 float v=smoothstep(.06,.3,b),e=length(vec2(bx,by));
 float lit=.5+.5*dot(normalize(vec3(-bx,-by,.08)),normalize(vec3(.4,.6,.7)));
 float age=mix(.8,.05+1.2*fr,fresh)*dy;vec3 col;
 if(surf==1){float ln=smoothstep(.015,.07,e)*smoothstep(.3,.1,e);col=(mix(c1,c2,smoothstep(.2,.4,b))*ln*1.5+c1*v*.06)*age+mix(c3,vec3(1.),.35)*ln*nw*.7+c1*.02;}
 else if(surf==2){float sp=pow(lit,9.);col=(mix(c1*.8,c2,smoothstep(.24,.44,b))*v*(.1+1.1*lit*lit)+mix(c3,vec3(1.),.6)*sp*v*.6)*age+mix(c3,vec3(1.),.35)*v*nw*.4+c1*.02;}
 else col=(mix(c1*.9,c2,smoothstep(.24,.44,b))*v*(.3+.8*lit)+c3*smoothstep(.02,.12,e)*.3*(.6+mid))*age+mix(c3,vec3(1.),.35)*v*nw*.55+c1*.02;
 if(halo>0.){float hb=0.;for(int i=0;i<8;i++){float a=float(i)*.7854;hb+=texture(dye,u+vec2(cos(a),sin(a))*px*(i%2==0?7.:13.)).g;}col+=mix(c2,c3,.4)*halo*smoothstep(.03,.3,hb/8.)*(1.-v*.7)*.55*age;}
 if(ringk>0.){float wave=0.;for(int i=0;i<6;i++){float ag=ons[i];if(ag<0.)continue;wave+=exp(-pow((rq-ag*1.5)*5.,2.))*exp(-ag*1.1);}col=col*(1.+wave*ringk*4.)+c3*wave*ringk*v*.5;}
 if(psy>0.)col=max(hrot(col,psy*(b*8.+length(q)*3.-t*.6)),0.);
 col*=(.8+.4*lvl)*(1.+flash*.7);
 o=vec4(col,1.);})GLSL";
static const char* const KAL = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform float rot,psy,segs,size,jag,lead,light,spark,frost,turn,tph,breathe,swell,wash,ripple,sfl,cstep,cflow,twist,bulge,layer,pph;uniform vec3 sb;uniform int surf,form,colm,cut;uniform float ons[6];
vec2 KR;
vec2 h22(vec2 p){return vec2(h21(p),h21(p+vec2(17.13,5.71)));}
vec2 pt(vec2 c){vec2 o=h22(c);return .5+.5*jag*sin(tph*(.6+o.yx)+6.2831853*o);}
/* the shards: x = distance to the nearest edge, y = distance from the middle of the piece; id names the piece */
vec2 shards(vec2 q,out vec2 id){vec2 n=floor(q),f=fract(q),mg=vec2(0.),mr=vec2(0.);float md=8.;
 for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++){vec2 g=vec2(float(i),float(j)),r=g+pt(n+g)-f;float d=dot(r,r);if(d<md){md=d;mr=r;mg=g;}}
 float bd=8.;
 for(int j=-2;j<=2;j++)for(int i=-2;i<=2;i++){vec2 g=mg+vec2(float(i),float(j)),r=g+pt(n+g)-f,dr=r-mr;if(dot(dr,dr)>1e-5)bd=min(bd,dot(.5*(mr+r),normalize(dr)));}
 id=n+mg;KR=mr;return vec2(bd,sqrt(md));}
/* the cut of the glass. shards: broken pieces. diamonds: a harlequin lattice whose rows slide. scales: overlapping rounds, like fish scales or roof tiles.
   rose: rings of wedges round a hub, like a rose window, each ring turning its own way. Shatter loosens each of them */
vec2 cutF(vec2 q,out vec2 id){
 if(cut==1){vec2 r=mat2(.7071,-.7071,.7071,.7071)*q;r.x+=jag*.5*sin(tph*.4+floor(r.y)*2.4);id=floor(r);vec2 f=fract(r)-.5;KR=f;return vec2(.5-max(abs(f.x),abs(f.y)),length(f));}
 if(cut==2){q.x+=jag*.22*sin(tph*.5+q.y*1.7);float R=.6,v=q.y*2.,bd=8.,md=0.;bool got=false;id=vec2(0.);KR=vec2(0.);
  for(int a=2;a>=-1;a--){float jj=floor(v)+float(a),off=.5*mod(jj,2.),ii=floor(q.x-off+.5);vec2 cc=vec2(ii+off,jj*.5);float dd=length(q-cc);
   if(dd<R){bd=min(bd,min(R-dd,.5-abs(q.x-off-ii)));md=dd;KR=q-cc;id=vec2(ii*2.+mod(jj,2.),jj);got=true;break;}bd=min(bd,dd-R);}
  if(!got)bd=0.;return vec2(bd,md);}
 if(cut==3){float rr=length(q),ri=floor(rr*1.1),n=max(3.,floor(6.2832*(ri+.5)/1.1)),sg=6.2832/n,aa=mod(atan(q.y,q.x)+h21(vec2(ri,3.))*6.2832+jag*tph*.12*(mod(ri,2.)*2.-1.),6.2832),si=floor(aa/sg),fa=fract(aa/sg),fr=fract(rr*1.1);
  float am=atan(q.y,q.x)+(.5-fa)*sg;vec2 cc=(ri+.5)/1.1*vec2(cos(am),sin(am));id=vec2(ri,si);KR=q-cc;return vec2(min(min(fr,1.-fr)/1.1,min(fa,1.-fa)*sg*rr),length(KR));}
 return shards(q,id);}
/* the mirrors. wheel: two mirrors make a rosette. tiles: three make a field of triangles. facets: a cut lens repeats a small rosette.
   sm = how far this point is from the nearest join between mirrors */
vec2 wheelF(vec2 p,float n,out float sm){float sg=3.14159265/n,r=length(p),a=atan(p.y,p.x);a=abs(mod(a+sg,2.*sg)-sg);sm=min(a,sg-a)*r;return r*vec2(cos(a),sin(a));}
vec2 triF(vec2 p,float ri,out float sm){for(int k=0;k<40;k++){bool inside=true;for(int i=0;i<3;i++){float an=1.5707963+2.0943951*float(i);vec2 nn=vec2(cos(an),sin(an));float d=dot(p,nn)-ri;if(d>0.){p-=2.*d*nn;inside=false;}}if(inside)break;}
 sm=1e3;for(int i=0;i<3;i++){float an=1.5707963+2.0943951*float(i);sm=min(sm,ri-dot(p,vec2(cos(an),sin(an))));}return p;}
vec2 hexF(vec2 p,float sz,out float sm,out vec2 hc){vec2 s=vec2(1.,1.7320508)*sz,a=mod(p,s)-s*.5,b=mod(p-s*.5,s)-s*.5,l=dot(a,a)<dot(b,b)?a:b;hc=p-l;vec2 al=abs(l);sm=sz*.5-max(al.x,al.x*.5+al.y*.8660254);return l;}
void main(){float pxp=2.*(1.-pz*.5)*fwidth(uv.y);vec2 p=(uv-.5)*vec2(asp,1.)*2.*(1.-pz*.5);float r0=length(p),ca=cos(rot),sa=sin(rot);
 /* each hit sends one slow, wide ring out from the middle */
 float wave=0.;for(int i=0;i<6;i++){float ag=ons[i];if(ag<0.)continue;wave+=exp(-pow((r0-ag*.75)*2.4,2.))*exp(-ag*.65)*smoothstep(0.,.3,ag);}wave=min(wave,1.4);
 p+=p/max(r0,1e-3)*wave*ripple*.1;p=mat2(ca,-sa,sa,ca)*p;
 /* bulge: a lens over the middle makes the centre pieces bigger. twist: the mirrors wind into a spiral */
 p*=1.-bulge*.6*exp(-r0*r0*1.3);{float ta=twist*3.2*r0/(1.+r0*.9),tc=cos(ta),ts=sin(ta);p=mat2(tc,-ts,ts,tc)*p;}
 float sm,k=(3.+(1.-size)*8.)/(1.+swell*.34*sb.x);vec2 d;
 if(form==1)d=triF(p,.62-segs*.035,sm);
 else if(form==2){vec2 hc;float sz=1.5-segs*.085,s2;vec2 l=hexF(p,sz,sm,hc);d=wheelF(l*(2.2/sz),3.,s2)+hc*.37;sm=min(sm,s2*sz/2.2);k*=.5;pxp*=2.2/sz;sm*=2.2/sz;}
 else d=wheelF(p,segs,sm);
 float ct=cos(turn*.8),st=sin(turn*.8);vec2 q=mat2(ct,-st,st,ct)*d*k+vec2(sin(turn*.6),cos(turn*.47))*2.+vec2(tph*.11,-tph*.07);
 vec2 id;vec2 sh=cutF(q,id);float bd=sh.x,md=sh.y,px=pxp*k;
 float h=h21(id),h2=h21(id+31.7),h3=h21(id+57.3);
 /* each piece takes one of the three colours; a few are pale, nearly clear glass */
 float band=h3<.34?sb.x:(h3<.67?sb.y:sb.z);
 /* how the music changes the colours. ring: each hit sends a ring of new colour outward. swap: on each hit a share of the pieces trade to their next colour, one after another.
    flow: the colours drift through the palette, faster when the music is louder. bands: each piece shifts with its own part of the sound */
 float cs=colm==1?step(h3,wash)*(floor(cstep)+smoothstep(h2*.6,h2*.6+.4,fract(cstep))):colm==2?cflow+.3*sin(h3*6.2832+cflow*1.7):colm==3?wash*band*2.:wash*wave*1.3;
 vec3 pc=pal(floor(h*3.)+.3*(h2-.5)+cs+wash*sb.z*.35*sin(h3*6.2832+t*.3));pc=pc*pc*(.75+.5*h3)+pc*.12;pc=mix(pc,vec3(1.),step(.92,h2)*.45);
 /* the lamp behind the glass is steady and brightest in the middle; breathe lets each piece swell a little, slowly, with its own part of the sound */
 float back=.28+light*(.32+.42*exp(-r0*r0*.6))+breathe*band*.42+sfl*.1;
 float tex=1.-frost*.6+frost*(.6*fbm(q*2.3+id*3.7)+.35*vn(q*9.+id)+.13*sin(dot(q,vec2(cos(h*6.2832),sin(h*6.2832)))*14.+h2*6.)+.2);
 float lw=.02+lead*.1,aa=px*1.2,pane=smoothstep(lw,lw+aa,bd);vec3 col;
 if(surf==1){float dome=smoothstep(lw,lw+.26,bd);vec2 hl=KR-vec2(.14,-.14);
  col=pc*(.1+back*1.3*tex)*dome*(.5+.7*dome)+mix(pc,vec3(1.),.7)*exp(-dot(hl,hl)*42.)*pane*(.45+spark*.35+breathe*band*.3);}
 else if(surf==2){float e=abs(bd-lw-.03);col=pc*(exp(-e/(aa+.006))*1.1+exp(-e/.07)*.3*glow)*(.45+back*1.1)+pc*.06*pane*back;}
 else{col=pc*back*tex*pane*1.35*(.62+.38*smoothstep(lw,lw+.3,bd))+mix(pc,vec3(1.),.35)*smoothstep(lw+.08,lw+aa,bd)*pane*(.16+.16*light+.25*breathe*band)+vec3(.022,.022,.026)*(1.-pane);}
 /* layers: a second sheet of bigger, paler glass lies over the first and turns the other way, so colours mix where they overlap */
 if(layer>0.){vec2 kr=KR,id2;float lc=cos(1.-turn*.5),ls=sin(1.-turn*.5);vec2 s2=shards(mat2(lc,-ls,ls,lc)*d*k*.42+vec2(7.3-tph*.06,2.1+tph*.04),id2);KR=kr;
  float g1=h21(id2+11.),p2=smoothstep(.03,.03+px*.6,s2.x);col*=mix(vec3(1.),mix(vec3(1.),pal(floor(g1*3.)+.5),.55)*1.3*(.3+.7*p2),layer);}
 float tw=pow(max(sin(t*(.45+h*.8)+h2*40.),0.),6.)*spark*(.4+.35*sb.z);col+=vec3(1.)*tw*exp(-md*md*70.)*pane;
 /* the joins between mirrors show as faint dark lines, and the cut edges of a faceted lens catch the light */
 col*=mix(.35,1.,smoothstep(0.,pxp*1.5+.004,sm));if(form==2)col+=mix(c2,vec3(1.),.6)*exp(-sm/.05)*.1;
 col*=mix(1.,.55,smoothstep(.4,1.9,r0));
 /* psychedelic turns every piece's hue by its own amount. It can swing below zero (the LFO), which turns the hues the other way.
    pph is the slow drift, counted up outside so that moving the control never jumps the colours */
 if(psy!=0.)col=max(hrot(col,psy*(h*5.+r0*2.+9.)-pph),0.);
 o=vec4(col,1.);})GLSL";
static const char* const LOTUS = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform float rot,psy,pph,zf,zr,zw,zc,fa,fb,fc,fo,lead,frost,light,wash,cstep,cflow;uniform vec3 sb;uniform int surf,form,colm;uniform float ons[6];
void main(){float pxl=2.*(1.-pz*.5)*fwidth(uv.y);vec2 p=(uv-.5)*vec2(asp,1.)*2.*(1.-pz*.5);float r0=length(p),ca=cos(rot),sa=sin(rot);p=mat2(ca,-sa,sa,ca)*p;
 /* each hit sends one slow, wide ring out from the middle; it carries a change of colour, not light */
 float wave=0.;for(int i=0;i<6;i++){float ag=ons[i];if(ag<0.)continue;wave+=exp(-pow((r0-ag*.75)*2.4,2.))*exp(-ag*.65)*smoothstep(0.,.3,ag);}wave=min(wave,1.4);
 float r=max(r0,1e-5),N=4.+floor(fa*8.+.5),K=N/6.2831853,y=log(r)*K-zf,x=atan(p.y,p.x)*K+fb*.6*log(r)*K-zr,fw=K*pxl/r;
 /* the petal's outline. petals: round at the base, drawn in to a point. scales: short and round, like roof tiles. spikes: narrow stars that show the ring beneath */
 float R=.54+fo*.16+.07*sb.x,wb=form==2?1.6:1.,wp=form==1?.5:form==2?4.:2.4,ys=form==1?1.3:1.,bo=8.,bi=8.,tl=0.,dxo=0.,jo=0.,io=0.;bool got=false;
 for(int a=-1;a<=2;a++){float jj=floor(y*2.)+float(a),off=.5*mod(jj,2.),ii=floor(x-off+.5),dx=x-off-ii,dy=y-jj*.5,wd=wb+fc*wp*max(dy,0.),dd=length(vec2(dx*wd,dy>0.?dy*ys:dy));
  if(dd<R){bo=min(R-dd,(.5-abs(dx))*1.4);tl=dy/R;dxo=dx*wd;jo=jj;io=ii;got=true;break;}
  bi=min(bi,dd-R);}
 vec3 core=mix(c3,vec3(1.),.4)*(surf==1?.5+.6*light:1.05),col;float hp=0.;
 if(!got)col=vec3(.012);
 else{vec2 pid=vec2(mod(io,N),mod(jo-zw,128.));hp=h21(pid);float h2=h21(pid+31.7),h3=h21(pid+57.3),band=h3<.34?sb.x:(h3<.67?sb.y:sb.z);
  /* how the music changes the colours. ring: each hit sends a ring of new colour outward. swap: on each hit a share of the petals trade to their next colour, one after another.
     flow: the colours drift through the palette, faster when the music is louder. bands: each petal shifts with its own part of the sound */
  float cs=colm==1?step(h3,wash)*(floor(cstep)+smoothstep(h2*.6,h2*.6+.4,fract(cstep))):colm==2?cflow+.3*sin(h3*6.2832+cflow*1.7):colm==3?wash*band*2.:wash*wave*1.3;
  vec3 pc=pal(jo*.23+zc+.3*(hp-.5)+cs+wash*sb.z*.35*sin(h3*6.2832+t*.3));
  if(surf==1){/* glass: every petal is a pane of stained glass, leaded along its edges, with a steady lamp behind that is brightest in the middle */
   float eb=min(bo,bi),lw=.008+lead*.045,aa=fw*1.3,pane=smoothstep(lw,lw+aa,eb);vec2 q=vec2(dxo,tl);
   float tex=1.-frost*.6+frost*(.6*fbm(q*3.1+hp*37.)+.35*vn(q*11.+hp*9.)+.13*sin(dot(q,vec2(cos(hp*6.2832),sin(hp*6.2832)))*14.+h2*6.)+.2);
   float back=.28+light*(.32+.42*exp(-r0*r0*.6));
   pc=pc*pc*(.75+.5*h3)+pc*.12;pc=mix(pc,vec3(1.),step(.92,h2)*.45);
   col=pc*back*tex*pane*1.35*(.62+.38*smoothstep(lw,lw+.3,eb))+mix(pc,vec3(1.),.35)*smoothstep(lw+.08,lw+aa,eb)*pane*(.16+.16*light)+vec3(.022,.022,.026)*(1.-pane);}
  else if(surf==2){pc=pc*pc*.85+pc*.15;col=pc*(exp(-bo/(fw*1.5+.012))*1.1+exp(-bo/.1)*.3*glow)+pc*.05;}
  else{/* soft: each petal darkens toward its hidden base, carries the shadow of the petal lying over it, and catches light along its rim and its vein */
   pc=pc*pc*.85+pc*.15;col=pc*(.3+.75*smoothstep(-.6,.9,tl))*(.35+.65*smoothstep(0.,.3,bi));
   col+=mix(pc,vec3(1.),.5)*(exp(-bo*13.)*.3+exp(-abs(dxo)*24.)*.16*smoothstep(-.3,.4,tl));
   col*=smoothstep(0.,fw*1.6+.004,bo);}}
 /* the very middle, where the petals are too small to draw, is a small steady glow */
 col=mix(col,core,smoothstep(.12,.4,fw));
 col*=mix(1.,.6,smoothstep(.6,2.,r0));
 /* psychedelic turns every petal's hue by its own amount; it can swing below zero (the LFO). pph is the slow drift, counted up outside so moving the control never jumps the colours */
 if(psy!=0.)col=max(hrot(col,psy*(hp*5.+r0*2.+9.)-pph),0.);
 o=vec4(col,1.);})GLSL";
static const char* const PIC_STEP = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D pic,pic2;uniform float psz,pasp,plain,amt,lifted,reach;uniform int src,act;uniform vec2 pctr;
float plum(vec3 c){return dot(c,vec3(.3,.59,.11));}
/* where a point of the screen falls on the picture, 0 to 1 across it */
vec2 picUV(vec2 q){vec2 e=pasp>=1.?vec2(psz,psz/pasp):vec2(psz*pasp,psz);return (q-vec2(asp*.5,.5)-pctr)/e+.5;}
float inPic(vec2 u,float f){vec2 m=min(u,1.-u);return smoothstep(0.,f,min(m.x,m.y));}
/* a colour bright enough to see as smoke: the dark parts of a picture give pale grey smoke */
vec3 lift(vec3 c){float b=max(c.r,max(c.g,c.b));return mix(vec3(.7,.74,.8),c*max(1.,.7/max(b,.08)),smoothstep(.05,.2,b));}
/* how strongly each point of the picture gives off smoke. shape: everything that stands out from the picture's edge. edges: only its outlines. lights: only its bright parts */
float srcAt(vec2 u){float ins=inPic(u,.03);if(ins<=0.)return 0.;vec4 a=textureLod(pic,u,1.),b=textureLod(pic,u,4.);float lo=mix(.8,.04,amt),s;
 if(src==1){float e=(1.-abs(textureLod(pic,u,3.).a*2.-1.))*mix(.25,1.,plain)+abs(plum(a.rgb)-plum(b.rgb))*2.2*b.a;s=smoothstep(lo,lo+.25,e)*2.2;}
 else if(src==2)s=smoothstep(lo,lo+.2,plum(a.rgb)*mix(1.,a.a,plain));
 else s=smoothstep(lo,lo+.25,a.a);
 return s*ins;}
/* how far a point is from the outline of the shape, in screen heights: above zero outside it, below zero inside. sdN points straight away from the shape */
float sdAt(vec2 q){vec2 pu=picUV(q),cu=clamp(pu,0.,1.),t=texture(pic2,cu).rg,e=pasp>=1.?vec2(1.,1./pasp):vec2(pasp,1.);return (t.r-t.g+length((pu-cu)*e))*psz;}
vec2 sdN(vec2 q){float g=.02;vec2 n=vec2(sdAt(q+vec2(g,0.))-sdAt(q-vec2(g,0.)),sdAt(q+vec2(0.,g))-sdAt(q-vec2(0.,g)));return n/max(length(n),1e-4);}
/* the shape as a rounded 3D form, like a pressed pillow: nothing at its outline, rising quickly to a soft top inside */
float dome(float sd){float t=clamp(-sd/(psz*.17),0.,1.);return sqrt(max(t*(2.-t),0.));}
/* the outline that the 3D form is built on. A shape on a plain ground: the shape's own outline. A picture that fills its frame has no outline to round off, so its whole frame is the form */
float sdForm(vec2 q){if(plain>.5)return sdAt(q);vec2 pu=picUV(q),e=pasp>=1.?vec2(1.,1./pasp):vec2(pasp,1.),m=min(pu,1.-pu)*e;return -min(m.x,m.y)*psz;}
/* how much the picture's own detail raises the form, like a carving: its lights on a plain ground, or whatever stands out in a picture that fills its frame */
float carve(vec2 q){vec4 a=textureLod(pic,clamp(picUV(q),0.,1.),2.8);return plain>.5?.8+.4*plum(a.rgb):.4+.75*a.a;}
uniform sampler2D prev;uniform float dt,fade,swirl,wisp,rise,wind,puff,emit,cph,bv,cloud,heal,ringk,blow;uniform int dir,colm;uniform vec4 dp;uniform vec3 k1,k2,k3;uniform float ons[6];
vec2 curl(vec2 s){float e=.015;return vec2(fbm(s+vec2(0,e))-fbm(s-vec2(0,e)),-(fbm(s+vec2(e,0))-fbm(s-vec2(e,0))))/(2.*e);}
/* the colour of smoke that does not come straight off the picture: slow patches in the picture's three main colours (a little greyed, so it stays calm), or the hue ring's */
vec3 cloudCol(vec2 q){float n=fbm(q*1.6+vec2(ph*.03,-ph*.02)+7.3);
 vec3 k=colm==1?pal(n*4.+cph):mix(mix(k1,k2,smoothstep(.3,.5,n)),k3,smoothstep(.48,.68,n));return mix(vec3(plum(k)),k,.82);}
void main(){vec2 q=vec2(uv.x*asp,uv.y),pu=picUV(q);float sd=sdAt(q),so=max(sd,0.);vec2 n=sdN(q),fd=dir==2?vec2(0.,-1.):vec2(0.,1.);
 vec2 v=curl(q*1.5+vec2(ph*.05,-ph*.07))*swirl+curl(q*4.7+vec2(-ph*.11,ph*.09))*wisp;
 /* thick smoke moves a little faster than thin, so it gathers into plumes */
 float dn=min(1.,plum(texture(prev,uv).rgb)*1.2),push=rise*(.82+.18*dn),thin=1.;
 if(act==1){/* beats: the puff of cloud mostly hangs where it is. The shape pumps in and out like a drum skin, and each hit sends a short ring out through the puff */
  v*=.45;v+=(dir==1?n:fd)*push*.18*smoothstep(0.,.05,sd);v+=n*bv*exp(-sd*sd*25.);
  for(int i=0;i<6;i++){float ag=ons[i];if(ag<0.)continue;float x=(sd-ag*.5)*8.;v+=n*ringk*exp(-x*x)*exp(-ag*1.4);}}
 else if(act==2){/* parts: one stream. It cannot pass through the shape, so it slides round it, and it is drawn back toward the shape so it wraps it instead of drifting off. spread: it circles the shape */
  vec2 t=vec2(-n.y,n.x);v*=.5;v+=dir==1?t*push*.8:fd*push;
  /* whatever the shape blocks is turned along its outline, so the stream slides round it quickly instead of piling up */
  float w=smoothstep(.06,0.,sd),vn=min(dot(v,n),0.);v-=n*vn*w;v-=t*sign(dot(v,t))*vn*w*.8;v+=n*w*.05;v-=n*push*.5*smoothstep(reach*.15,reach*.7,so);}
 /* pours and melts. spread: the smoke leaves the shape in every direction. It has more and more room to fill, so it thins as it goes */
 else if(dir==0)v.y+=push;else if(dir==2)v.y-=push;else{float sp=push*(.35+.65/(1.+so*so*6.));v+=n*sp;thin=exp(-dt*.11*sp/(so+.5));}
 v.x+=wind*sin(ph*.13+q.y*2.3+2.*sin(ph*.05));
 /* each hit shoves the smoke off the outline of the shape */
 v+=n*puff*(act==2?.3:1.)*exp(-sd*sd*60.);
 /* melts: while the music is quiet the picture holds together and only frays at its edges; lows and hits blow right through it */
 if(act==3)v*=mix(1.,blow,min(srcAt(pu),1.));
 vec2 dd=q-dp.xy;v+=dp.zw*exp(-dot(dd,dd)*40.);
 vec2 bk=uv-v*dt*vec2(1./asp,1.)*.11,mb=min(bk,1.-bk);vec2 tx=1.5/vec2(textureSize(prev,0));
 /* a little of each neighbour is mixed in, so the smoke softens and spreads as it ages */
 vec3 c=mix(texture(prev,bk).rgb,(texture(prev,bk+vec2(tx.x,0.)).rgb+texture(prev,bk-vec2(tx.x,0.)).rgb+texture(prev,bk+vec2(0.,tx.y)).rgb+texture(prev,bk-vec2(0.,tx.y)).rgb)*.25,.12)*thin;
 /* Reach: smoke that strays this far from the shape thins away, so the smoke stays gathered round the shape and the rest of the screen stays dark */
 float far=reach*(act==1?.5:act==3?.7:1.);c*=smoothstep(0.,.012,min(mb.x,mb.y))*exp(-dt*6.*smoothstep(far*.6,far*1.4,so));
 if(act==1){/* beats: a puff of cloud that fills the shape and reaches a little way past its outline. It is always drifting back to rest, so whatever a beat does to it heals over */
  float prof=exp(-so*so/(reach*reach*.09)),d=.45+.55*smoothstep(.25,.7,fbm(q*2.6+vec2(-ph*.05,ph*.04)));vec3 bc=cloudCol(q);
  if(colm==0)bc=mix(bc,lift(textureLod(pic,clamp(pu,0.,1.),2.5).rgb),smoothstep(.01,-.02,sd)*inPic(pu,.02));
  c=mix(c,bc*d*cloud*prof,1.-exp(-dt*heal));}
 else if(act==2){/* parts: the stream starts in a band just upstream of the shape (all round it, for spread), in strands, and nothing gets inside the shape */
  c*=fade*smoothstep(-.012,.02,sd);float up=dir==1?1.:smoothstep(.1,-.5,dot(n,fd)),x=(sd-reach*(dir==1?.3:.4))/(reach*.2);
  c+=cloudCol(q)*up*exp(-x*x)*(.25+1.5*smoothstep(.3,.7,fbm(q*6.+vec2(ph*.21,-ph*.5))))*emit*dt*1.6;}
 else{c*=fade;float s=srcAt(pu);
  if(s>0.){vec3 pc=textureLod(pic,pu,act==3?0.:1.).rgb;
   if(act==3){/* melts: the picture is made of smoke. The flow carries it away and it keeps re-forming */
    pc=colm==1?pal(plum(pc)*1.6+pu.y*.9+cph):mix(pc,lift(pc),lifted);c=mix(c,pc,min(s,1.)*(1.-exp(-dt*heal)));}
   else{/* pours: the shape does not smoke evenly. Patches of it take turns, so the smoke leaves in strands */
    s*=.25+1.5*smoothstep(.3,.7,fbm(q*6.+vec2(ph*.21,-ph*.5)));c+=(colm==1?pal(plum(pc)*1.6+pu.y*.9+cph):lift(pc))*s*emit*dt;}}}
 /* however much smoke piles up it keeps its colour and never burns out to white */
 o=vec4(c*min(1.,2.2/max(max(c.r,max(c.g,c.b)),1e-4)),1.);})GLSL";
static const char* const PIC_SHOW = R"GLSL(#version 330 core
precision highp float;in vec2 uv;out vec4 o;
uniform float t,asp,glow,flash,low,mid,high,lvl,ph,pz;uniform vec3 c1,c2,c3;
vec3 pal(float i){i=mod(i,3.);return i<1.?mix(c1,c2,i):i<2.?mix(c2,c3,i-1.):mix(c3,c1,i-2.);}
float h21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),mix(h21(i+vec2(0,1)),h21(i+1.),f.x),f.y);}
float fbm(vec2 p){float s=0.,a=.5;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return s;}
vec3 hrot(vec3 c,float a){const vec3 k=vec3(.57735);float ca=cos(a),sa=sin(a);return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca);}
/* mirror: off, 2, 4, 6 or 8 matching pieces. 6 and 8 fold the picture round the middle like a kaleidoscope, always matching left to right */
vec2 kal(vec2 d,int m){if(m==1)d.x=abs(d.x);else if(m==2)d=abs(d);else if(m>2){float sg=6.2831853/(m==3?3.:4.),a=atan(d.x,d.y);a=abs(mod(a+sg*.5,sg)-sg*.5);d=length(d)*vec2(sin(a),cos(a));}return d;}
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D pic,pic2;uniform float psz,pasp,plain,amt,lifted,reach;uniform int src,act;uniform vec2 pctr;
float plum(vec3 c){return dot(c,vec3(.3,.59,.11));}
/* where a point of the screen falls on the picture, 0 to 1 across it */
vec2 picUV(vec2 q){vec2 e=pasp>=1.?vec2(psz,psz/pasp):vec2(psz*pasp,psz);return (q-vec2(asp*.5,.5)-pctr)/e+.5;}
float inPic(vec2 u,float f){vec2 m=min(u,1.-u);return smoothstep(0.,f,min(m.x,m.y));}
/* a colour bright enough to see as smoke: the dark parts of a picture give pale grey smoke */
vec3 lift(vec3 c){float b=max(c.r,max(c.g,c.b));return mix(vec3(.7,.74,.8),c*max(1.,.7/max(b,.08)),smoothstep(.05,.2,b));}
/* how strongly each point of the picture gives off smoke. shape: everything that stands out from the picture's edge. edges: only its outlines. lights: only its bright parts */
float srcAt(vec2 u){float ins=inPic(u,.03);if(ins<=0.)return 0.;vec4 a=textureLod(pic,u,1.),b=textureLod(pic,u,4.);float lo=mix(.8,.04,amt),s;
 if(src==1){float e=(1.-abs(textureLod(pic,u,3.).a*2.-1.))*mix(.25,1.,plain)+abs(plum(a.rgb)-plum(b.rgb))*2.2*b.a;s=smoothstep(lo,lo+.25,e)*2.2;}
 else if(src==2)s=smoothstep(lo,lo+.2,plum(a.rgb)*mix(1.,a.a,plain));
 else s=smoothstep(lo,lo+.25,a.a);
 return s*ins;}
/* how far a point is from the outline of the shape, in screen heights: above zero outside it, below zero inside. sdN points straight away from the shape */
float sdAt(vec2 q){vec2 pu=picUV(q),cu=clamp(pu,0.,1.),t=texture(pic2,cu).rg,e=pasp>=1.?vec2(1.,1./pasp):vec2(pasp,1.);return (t.r-t.g+length((pu-cu)*e))*psz;}
vec2 sdN(vec2 q){float g=.02;vec2 n=vec2(sdAt(q+vec2(g,0.))-sdAt(q-vec2(g,0.)),sdAt(q+vec2(0.,g))-sdAt(q-vec2(0.,g)));return n/max(length(n),1e-4);}
/* the shape as a rounded 3D form, like a pressed pillow: nothing at its outline, rising quickly to a soft top inside */
float dome(float sd){float t=clamp(-sd/(psz*.17),0.,1.);return sqrt(max(t*(2.-t),0.));}
/* the outline that the 3D form is built on. A shape on a plain ground: the shape's own outline. A picture that fills its frame has no outline to round off, so its whole frame is the form */
float sdForm(vec2 q){if(plain>.5)return sdAt(q);vec2 pu=picUV(q),e=pasp>=1.?vec2(1.,1./pasp):vec2(pasp,1.),m=min(pu,1.-pu)*e;return -min(m.x,m.y)*psz;}
/* how much the picture's own detail raises the form, like a carving: its lights on a plain ground, or whatever stands out in a picture that fills its frame */
float carve(vec2 q){vec4 a=textureLod(pic,clamp(picUV(q),0.,1.),2.8);return plain>.5?.8+.4*plum(a.rgb):.4+.75*a.a;}
uniform sampler2D dye;uniform vec2 px;uniform float rot,psy,pph,vis,haze,ash,bx,dent,ringk;uniform int surf,mir;uniform float ons[6];
float dl(vec2 u){vec3 c=texture(dye,u).rgb;return (c.r+c.g+c.b)*.42;}
/* how high the shape stands at a point: the rounded form, with the picture's own lights raised a little like a carving, on a low mound that falls away round it (so the smoke round the shape is lit as part of the same solid). beats: every hit punches the form in to a dent and it springs back out, and sends a short ring through the puff round it */
float form(vec2 q){float sd=sdForm(q),so=max(sd,0.),h=(dome(sd)*carve(q)+.32*exp(-so*so/(reach*reach*.12)))*dent;
 if(act==1){h*=.55-1.5*clamp(bx,-.5,1.1);float w=smoothstep(0.,.04,sd)*exp(-2.*max(sd,0.)/reach);
  for(int i=0;i<6;i++){float ag=ons[i];if(ag<0.)continue;float x=(sd-ag*.5)*10.;h+=ringk*x*exp(-x*x)*exp(-ag*1.4)*w;}}
 return h;}
void main(){vec2 d=(uv-.5)*vec2(asp,1.)*(1.-pz);float r0=length(d),ca=cos(rot),sa=sin(rot);
 /* spin turns the whole picture first, then the mirrors fold it. With 6 or 8 mirrors the fold sits at the foot of the smoke's field, so a whole wedge fits.
    Past its edges the smoke's field repeats as a mirror image, so spin and wide mirrors never run out of picture */
 d=kal(mat2(ca,-sa,sa,ca)*d,mir);vec2 q=d+(mir>2?vec2(asp*.5,0.):vec2(asp*.5,.5)),u=q/vec2(asp,1.);
 vec3 c=texture(dye,u).rgb*.4+(texture(dye,u+px).rgb+texture(dye,u-px).rgb+texture(dye,u+vec2(px.x,-px.y)).rgb+texture(dye,u+vec2(-px.x,px.y)).rgb)*.15;
 vec2 pk=px*1.6;float hx=dl(u+vec2(pk.x,0.))-dl(u-vec2(pk.x,0.)),hy=dl(u+vec2(0.,pk.y))-dl(u-vec2(0.,pk.y)),lm=c.r+c.g+c.b,edge=length(vec2(hx,hy)),ns=surf==1?8.:.9;
 /* the slope of the form tilts the light on the smoke and on the picture alike, which is what makes the shape read as solid */
 float g=.006,fh=form(q),gx=(form(q+vec2(g,0.))-form(q-vec2(g,0.)))/(2.*g),gy=(form(q+vec2(0.,g))-form(q-vec2(0.,g)))/(2.*g),sdq=sdForm(q);
 vec3 n=normalize(vec3(-hx*ns-gx*.2,-hy*ns-gy*.2,1.)),n2=normalize(vec3(-gx*.2,-gy*.2,1.)),ld=normalize(vec3(-.4,.55,.75));
 float dif=max(dot(n,ld),0.),dif2=max(dot(n2,ld),0.),mx=max(c.r,max(c.g,c.b));
 vec3 hue=c/max(mx,.1);
 /* ash: where the smoke thins out it loses its colour and goes grey */
 hue=mix(hue,vec3(plum(hue))*vec3(.94,.97,1.06)+.12,ash*(1.-smoothstep(0.,1.1,lm))*(surf==1?.3:1.)*(act==3?0.:1.));
 /* a picture made of smoke keeps its own lights and darks; other smoke just gets denser */
 float dens0=act==3?min(mx,1.):1.-exp(-lm*.75);
 vec3 col;float cov;
 if(surf==2){/* neon: the smoke drawn as thin glowing contour lines, which climb the form like the lines on a map */
  float lv=lm*1.7+fh*4.,f=abs(fract(lv)-.5),fw=max(fwidth(lv),1e-4),ln=smoothstep(fw*1.7,0.,f)+exp(-f/(fw*5.+.03))*.35*glow,sf=1.-exp(-lm*.8);col=hue*ln*.7*smoothstep(.02,.2,lm)+hue*sf*.05;cov=min(1.,sf*.5);}
 else if(surf==1){/* oil: thick and rich, with a wet shine */
  float dens=act==3?dens0:pow(1.-exp(-lm*2.6),.8),spec=pow(max(dot(reflect(-ld,n),vec3(0,0,1)),0.),40.);col=hue*hue*dens*(.24+.62*dif)+hue*dens*.08+vec3(1.)*spec*.35*smoothstep(0.,.04,edge+length(vec2(gx,gy))*.02)*min(dens*3.,1.);cov=dens;}
 else{/* smoke: thin and soft. The form gives it a lit side and a shaded side; its own folds are lit only gently */
  col=hue*dens0*(.32+.62*dif2+.3*(dif-dif2))*(act==3?mix(.8,1.2,smoothstep(.03,-.01,sdq)):1.);cov=dens0;}
 /* the picture itself, steady, lit as the same 3D form, with a soft shine where it turns toward the light. pours and parts: it stands in front of the smoke, bent a little by it like heat haze.
    beats: it sits on the puff and is punched in with it. melts: the smoke is the picture */
 vec2 pu=picUV(q)+vec2(hx,hy)/(1.+edge*6.)*haze*.09;vec4 P=texture(pic,pu);float pa=mix(inPic(pu,.07),P.a*inPic(pu,.02),plain);
 vec3 pc=mix(P.rgb,lift(P.rgb),lifted)*(.4+.8*dif2)+vec3(1.)*pow(max(dot(reflect(-ld,n2),vec3(0,0,1)),0.),18.)*.16;
 if(act==1)col=mix(col,pc,pa*vis*.9);
 else if(act!=3)col=mix(col,pc,pa*vis*(1.-cov*.15));
 /* the form throws a soft shadow on the smoke behind it, on the side away from the light */
 float up=dent*(act==1?max(.55-1.5*bx,0.):1.);col*=1.-.5*min(up*1.8,1.)*smoothstep(0.,-.025,sdForm(q+ld.xy*.05))*smoothstep(-.004,.012,sdq);
 /* psychedelic turns the hues; pph is the slow drift, counted up outside so moving the control never jumps the colours */
 if(psy!=0.)col=max(hrot(col,psy*(lm*2.+r0*2.+9.)-pph),0.);
 o=vec4(max(col,0.),1.);})GLSL";
}
