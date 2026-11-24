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
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D dye;uniform vec2 px;uniform int mir;uniform float sharp,mono,rot,psy,trip,tw,segs,lsp;uniform int form,surf;
vec2 tri(vec2 s){return 1.-abs(1.-mod(s,2.));}
vec2 fold(vec2 u,float k){float on=step(.001,lsp);vec2 d=(u-.5)*vec2(asp,1.)*(1.-pz)*(1.+k*lsp);float ang=rot*(1.-k*.3*on)+k*1.1*on+tw/(1.+dot(d,d)*5.);float c=cos(ang),s=sin(ang);d=mat2(c,-s,s,c)*d;if(mir>0)d.x=abs(d.x);if(mir==2)d.y=abs(d.y);
 if(trip>0.){d+=trip*.05*sin(d.yx*7.+t*.8)*(1.+low*.6);for(int i=0;i<4;i++){float w=clamp(trip*4.-float(i),0.,1.);if(w<=0.)break;
  float a=ph*.04+float(i)*1.3,ca=cos(a),sa=sin(a);vec2 q=mat2(ca,-sa,sa,ca)*(abs(d)*1.55-vec2(.5,.36));d=mix(d,q,w*w*(3.-2.*w));}}
 vec2 r;
 if(form==1){float sg=6.2831853/segs,a=abs(mod(atan(d.y,d.x),sg)-sg*.5),rr=length(d);r=rr*vec2(cos(a),sin(a))*2./vec2(asp,1.);}
 else if(form==2){float a=atan(d.y,d.x)/6.2831853+.5;r=vec2(abs(fract(a*segs)-.5)*2.,length(d)*1.7);}
 else if(form==3){r=d*segs*.5/vec2(asp,1.);}
 else if(mir==2)r=d*2./vec2(asp,1.);else if(mir==1)r=vec2(d.x*2./asp,d.y+.5);else r=d/vec2(asp,1.)+.5;
 return tri(r+k*vec2(.37,.23)*on);}
vec3 dyeAt(vec2 u,vec2 b){return b.x>0.?(texture(dye,u+b).rgb+texture(dye,u-b).rgb+texture(dye,u+vec2(b.x,-b.y)).rgb+texture(dye,u+vec2(-b.x,b.y)).rgb)*.25:texture(dye,u).rgb;}
float lum(vec2 u){vec3 c=texture(dye,u).rgb;return (c.r+c.g+c.b)*.42;}
/* one layer of ink: colour in rgb, how much it covers in a */
vec4 shade(vec2 uvp,float k){vec2 u=fold(uvp,k);vec2 pk=px*(1.+k*1.6);vec3 c=dyeAt(u,k>0.?pk*1.3:vec2(0.));
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
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform vec3 bl[16];uniform int nb,surf,form;uniform float rot,psy,stirv,wob,lay;
vec2 fld(vec2 p,out vec2 ga,out vec2 gb){float fa=0.,fb=0.;ga=vec2(0.);gb=vec2(0.);for(int i=0;i<16;i++){if(i>=nb)break;vec2 d=p-bl[i].xy;float q=dot(d,d)+1e-3,r2=bl[i].z*bl[i].z,v=r2/q;vec2 g=-2.*r2*d/(q*q);if(i%2==0){fa+=v;ga+=g;}else{fb+=v;gb+=g;}}return min(vec2(fa,fb),vec2(14.));}
/* one colour of blob in the chosen look: lamp (glowing oil), glass (clear, with a rim and a highlight) or neon (outlines only) */
float PX,PA;float edge(float f,vec2 g){float w=max(.07,length(g)*PA);return smoothstep(1.01-w,1.01+w,f);}
vec3 look(float f,vec2 gf,vec3 ca,vec3 cb,vec3 cr,float cell){float gl=max(length(gf)*PX,1e-4),m=edge(f,gf),nr=smoothstep(3.5,1.6,f),e=abs(f-1.)/gl;
 if(surf==1){float h=sqrt(max(1.-1./max(f,1.),0.));vec2 g=gf/(2.*max(h,.06)*f*f)*smoothstep(11.,4.,f);vec3 n=normalize(vec3(-g*.2,1.));
  float sp=pow(max(dot(n,normalize(vec3(-.45,.6,.66))),0.),22.),fr=pow(1.-n.z,1.5);
  return ((ca*.09+cb*.10*smoothstep(1.,5.,f))*cell+cr*fr*.75+mix(cr,vec3(1.),.65)*sp*.9)*m+cr*exp(-e*.1)*.35*nr;}
 if(surf==2){float e2=abs(f-2.3)/gl;return cr*(exp(-e*.3)*1.15+exp(-e*.05)*.3*glow)*nr+cb*exp(-e2*.3)*.5*m*smoothstep(6.,3.5,f)+ca*.05*m;}
 return (mix(ca,cb,smoothstep(1.,4.,f))*(.5+.3*smoothstep(1.,7.,f))*cell+cr*exp(-e*.09)*.55*nr)*m;}
void main(){vec2 p=(uv-.5)*vec2(asp,1.)*2.*(1.-pz*.6);float ca=cos(rot),sa=sin(rot);p=mat2(ca,-sa,sa,ca)*p;if(form==3)p=abs(p);
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
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform vec4 md;uniform float bl,rot,psy,grain,shake,sharp;uniform int surf,form;
float chl(vec2 q,vec2 nm){return cos(nm.x*q.x)*cos(nm.y*q.y)-cos(nm.y*q.x)*cos(nm.x*q.y);}
/* the figure the grains settle on: a square plate, a round plate, or ripples crossing from three points */
float fig(vec2 g,vec2 nm){
 if(form==1){float r=length(g),th=atan(g.y,g.x);return cos(nm.x*th)*sin(nm.y*r*2.6+.6)+.3*cos((nm.x+nm.y)*r*1.7);}
 if(form==2){float d1=length(g-vec2(.6,0.)),d2=length(g+vec2(.6,0.)),d3=length(g-vec2(0.,.55));return .5*(cos(nm.x*4.2*d1)+cos(nm.y*4.2*d2))+.3*cos((nm.x+nm.y)*2.*d3);}
 return chl(g*1.5708,nm);}
void main(){vec2 p=(uv-.5)*vec2(asp,1.)*2.*(1.-pz*.5);float ca=cos(rot),sa=sin(rot);p=mat2(ca,-sa,sa,ca)*p;
 float G=(70.+grain*190.)*(surf==1?2.2:surf==2?.4:1.);vec2 cell=floor(p*G),f=fract(p*G)-.5;float h=h21(cell),h2=h21(cell+17.3);
 vec2 jit=surf==2?vec2(0.):(vec2(h2,h21(cell+5.1))-.5)*.7;vec2 gp=(cell+.5+jit)/G;
 gp+=shake*.07*(vec2(vn(cell*.25+t*11.),vn(cell*.25-t*9.+3.))-.5);
 float s=mix(fig(gp,md.xy),fig(gp,md.zw),bl);
 float w=(.07+shake*.3+bl*(1.-bl)*.5)*(1.75-sharp*1.5);float dens=exp(-abs(s)/w);
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
float L(float d){float fw=max(fwidth(d),1e-4);return smoothstep(fw*1.5,0.,abs(d))+exp(-abs(d)/(fw*4.+.01))*.32*glow;}
uniform sampler2D dye;uniform vec2 px;uniform float rot,psy,fresh;uniform int surf,mir;
void main(){vec2 q=(uv-.5)*vec2(asp,1.)*(1.-pz*.5);float ca=cos(rot),sa=sin(rot);q=mat2(ca,-sa,sa,ca)*q;if(mir>0)q.x=abs(q.x);if(mir==2)q.y=abs(q.y);vec2 u=q/vec2(asp,1.)+.5;
 vec2 hp=px*.42;vec4 dd=.25*(texture(dye,u+hp)+texture(dye,u-hp)+texture(dye,u+vec2(hp.x,-hp.y))+texture(dye,u+vec2(-hp.x,hp.y)));float b=dd.g,dy=1.-smoothstep(0.,.25,dd.a),fr=pow(1.-dd.b,1.7)*dy,nw=smoothstep(.22,0.,dd.b)*dy;float bx=texture(dye,u+vec2(px.x,0.)).g-texture(dye,u-vec2(px.x,0.)).g,by=texture(dye,u+vec2(0.,px.y)).g-texture(dye,u-vec2(0.,px.y)).g;
 float v=smoothstep(.06,.3,b),e=length(vec2(bx,by));
 float lit=.5+.5*dot(normalize(vec3(-bx,-by,.08)),normalize(vec3(.4,.6,.7)));
 float age=mix(.8,.05+1.2*fr,fresh)*dy;vec3 col;
 if(surf==1){float ln=smoothstep(.015,.07,e)*smoothstep(.3,.1,e);col=(mix(c1,c2,smoothstep(.2,.4,b))*ln*1.5+c1*v*.06)*age+mix(c3,vec3(1.),.35)*ln*nw*.7+c1*.02;}
 else if(surf==2){float sp=pow(lit,9.);col=(mix(c1*.8,c2,smoothstep(.24,.44,b))*v*(.1+1.1*lit*lit)+mix(c3,vec3(1.),.6)*sp*v*.6)*age+mix(c3,vec3(1.),.35)*v*nw*.4+c1*.02;}
 else col=(mix(c1*.9,c2,smoothstep(.24,.44,b))*v*(.3+.8*lit)+c3*smoothstep(.02,.12,e)*.3*(.6+mid))*age+mix(c3,vec3(1.),.35)*v*nw*.55+c1*.02;
 if(psy>0.)col=max(hrot(col,psy*(b*8.+length(q)*3.-t*.6)),0.);
 col*=(.8+.4*lvl)*(1.+flash*.7);
 o=vec4(col,1.);})GLSL";
}
