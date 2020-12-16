
bin_PROGRAMS = sdlquake

sdlquake_LDADD = @MATHLIB@ @INETLIB@

sdlquake_SOURCES =		\
	adivtab.hpp		\
	anorm_dots.hpp		\
	anorms.hpp		\
	asm_draw.hpp		\
	asm_i386.hpp		\
	block16.hpp		\
	block8.hpp		\
	bspfile.hpp		\
	cd_sdl.cpp		\
	cdaudio.hpp		\
	chase.cpp			\
	cl_demo.cpp		\
	cl_input.cpp		\
	cl_main.cpp		\
	cl_parse.cpp		\
	cl_tent.cpp		\
	clean.bat		\
	client.hpp		\
	cmd.cpp			\
	cmd.hpp			\
	common.cpp		\
	common.hpp		\
	conproc.hpp		\
	console.cpp		\
	console.hpp		\
	crc.cpp			\
	crc.hpp			\
	cvar.cpp			\
	cvar.hpp			\
	d_copy.S		\
	d_edge.cpp		\
	d_fill.cpp		\
	d_iface.hpp		\
	d_ifacea.hpp		\
	d_init.cpp		\
	d_local.hpp		\
	d_modech.cpp		\
	d_part.cpp		\
	d_polyse.cpp		\
	d_scan.cpp		\
	d_sky.cpp			\
	d_sprite.cpp		\
	d_surf.cpp		\
	d_zpoint.cpp		\
	dosasm.S		\
	dosisms.hpp		\
	draw.cpp			\
	draw.hpp			\
	host.cpp			\
	host_cmd.cpp		\
	input.hpp			\
	keys.cpp			\
	keys.hpp			\
	mathlib.cpp		\
	mathlib.hpp		\
	menu.cpp			\
	menu.hpp			\
	model.cpp			\
	model.hpp			\
	modelgen.hpp		\
	mpdosock.hpp		\
	net.hpp			\
	net_bsd.cpp		\
	net_bw.hpp		\
	net_dgrm.cpp		\
	net_dgrm.hpp		\
	net_loop.cpp		\
	net_loop.hpp		\
	net_main.cpp		\
	net_udp.cpp		\
	net_udp.hpp		\
	net_vcr.cpp		\
	net_vcr.hpp		\
	net_wso.cpp		\
	pr_cmds.cpp		\
	pr_comp.hpp		\
	pr_edict.cpp		\
	pr_exec.cpp		\
	progdefs.hpp		\
	progs.hpp			\
	protocol.hpp		\
	quakeasm.hpp		\
	quakedef.hpp		\
	r_aclip.cpp		\
	r_alias.cpp		\
	r_bsp.cpp			\
	r_draw.cpp		\
	r_edge.cpp		\
	r_efrag.cpp		\
	r_light.cpp		\
	r_local.hpp		\
	r_main.cpp		\
	r_misc.cpp		\
	r_part.cpp		\
	r_shared.hpp		\
	r_sky.cpp			\
	r_sprite.cpp		\
	r_surf.cpp		\
	r_vars.cpp		\
	r_varsa.S		\
	render.hpp		\
	resource.hpp		\
	sbar.cpp			\
	sbar.hpp			\
	scitech			\
	screen.cpp		\
	screen.hpp		\
	server.hpp		\
	snd_dma.cpp		\
	snd_mem.cpp		\
	snd_mix.cpp		\
	snd_sdl.cpp		\
	sound.hpp			\
	spritegn.hpp		\
	sv_main.cpp		\
	sv_move.cpp		\
	sv_phys.cpp		\
	sv_user.cpp		\
	sys.hpp			\
	sys_sdl.cpp		\
	vgamodes.hpp		\
	vid.hpp			\
	vid_sdl.cpp		\
	view.cpp			\
	view.hpp			\
	wad.cpp			\
	wad.hpp			\
	winquake.hpp		\
	world.cpp			\
	world.hpp			\
	zone.cpp			\
	zone.hpp			\
	$(X86_SRCS) $(NONX86_SRCS)

X86_SRCS =			\
	snd_mixa.S		\
	sys_dosa.S		\
	d_draw.S		\
	d_draw16.S		\
	d_parta.S		\
	d_polysa.S		\
	d_scana.S		\
	d_spr8.S		\
	d_varsa.S		\
	math.S			\
	r_aclipa.S		\
	r_aliasa.S		\
	r_drawa.S		\
	r_edgea.S		\
	surf16.S		\
	surf8.S			\
	worlda.S

NONX86_SRCS =			\
	d_vars.cpp		\
	nonintel.cpp


NULL_SRCS =			\
	cd_null.c		\
	in_null.c		\
	net_none.c		\
	snd_null.c		\
	sys_null.c		\
	vid_null.c

DOS_SRCS =			\
	cd_audio.c		\
	dos_v2.c		\
	in_dos.c		\
	mplib.c			\
	mplpc.c			\
	net_bw.c		\
	net_comx.c		\
	net_dos.c		\
	net_ipx.c		\
	net_ipx.h		\
	net_ser.c		\
	net_ser.h		\
	snd_dos.c		\
	snd_gus.c		\
	sys_dos.c		\
	vid_dos.c		\
	vid_dos.h		\
	vid_ext.c		\
	vid_vga.c		\
	vregset.c		\
	vregset.h

WIN_SRCS =			\
	cd_win.c		\
	conproc.c		\
	in_win.c		\
	net_mp.c		\
	net_mp.h		\
	net_win.c		\
	net_wins.c		\
	net_wins.h		\
	net_wipx.c		\
	net_wipx.h		\
	snd_win.c		\
	sys_win.c		\
	sys_wina.S		\
	sys_wind.c		\
	vid_win.c

LNX_SRCS =			\
	cd_linux.c		\
	snd_linux.c		\
	sys_linux.c		\
	vid_svgalib.c		\
	vid_x.c

SUN_SRCS =			\
	snd_sun.c		\
	in_sun.c		\
	sys_sun.c		\
	vid_sunx.c		\
	vid_sunxil.c

NEXT_SRCS =			\
	snd_next.c

GL_SRCS =			\
	gl_draw.c		\
	gl_mesh.c		\
	gl_model.c		\
	gl_model.h		\
	gl_refrag.c		\
	gl_rlight.c		\
	gl_rmain.c		\
	gl_rmisc.c		\
	gl_rsurf.c		\
	gl_screen.c		\
	gl_test.c		\
	gl_vidlinux.c		\
	gl_vidlinuxglx.c	\
	gl_vidnt.c		\
	gl_warp.c		\
	gl_warp_sin.h		\
	glquake.h		\
	glquake2.h

EXTRA_DIST =			\
	README.SDL		\
	3dfx.txt		\
	Makefile.Solaris	\
	Makefile.linuxi386	\
	README.Solaris		\
	WinQuake.dsp		\
	WinQuake.dsw		\
	WinQuake.mdp		\
	WinQuake.ncb		\
	WinQuake.opt		\
	WinQuake.plg		\
	cwsdpmi.exe		\
	glqnotes.txt		\
	makezip.bat		\
	progdefs.q1		\
	progdefs.q2		\
	q.bat			\
	qa.bat			\
	qb.bat			\
	qe3.ico			\
	qt.bat			\
	quake-data.spec.sh	\
	quake-hipnotic.spec.sh	\
	quake-rogue.spec.sh	\
	quake-shareware.spec.sh	\
	quake.gif		\
	quake.ico		\
	quake.spec.sh		\
	winquake.aps		\
	winquake.rc		\
	wq.bat			\
	wqreadme.txt		\
	$(NULL_SRCS)		\
	$(DOS_SRCS)		\
	$(WIN_SRCS)		\
	$(LNX_SRCS)		\
	$(SUN_SRCS)		\
	$(NEXT_SRCS)		\
	$(GL_SRCS)

dist-hook:
	cp -rp data docs dxsdk gas2masm kit scitech $(distdir)/
