Name:       ico-uxf-weston-plugin
Summary:    Weston Plugins for IVI
Version:    0.7.03
Release:    1.2
VCS:        profile/ivi/ico-uxf-weston-plugin#submit/tizen/20130823.164747-0-g55247b7a60bc44568bd3bdc10c9277e93a4220c2
Group:      Graphics & UI Framework/Automotive UI
License:    MIT
URL:        ""
Source0:    %{name}-%{version}.tar.bz2

BuildRequires: pkgconfig(weston) >= 1.2.1
BuildRequires: pkgconfig(pixman-1)
BuildRequires: pkgconfig(xkbcommon) >= 0.0.578
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(evas)
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(elementary)
BuildRequires: pkgconfig(ecore-wayland)
BuildRequires: mesa-devel
BuildRequires: pkgconfig(aul)
BuildRequires: pkgconfig(ecore)
Requires: weston >= 1.2.1

%description
Weston Plugins for IVI

%package devel
Summary:    Development files for %{name}
Group:      Graphics & UI Framework/Development
Requires:   %{name} = %{version}-%{release}

%description devel
Development files that expose the wayland extended protocols for IVI.

%prep
%setup -q -n %{name}-%{version}

%build
%autogen

%configure
make %{?_smp_mflags}

%install
%make_install

# configurations
%define weston_conf %{_sysconfdir}/xdg/weston
mkdir -p %{buildroot}%{weston_conf} > /dev/null 2>&1
install -m 0644 settings/weston.ini %{buildroot}%{weston_conf}
mkdir -p %{buildroot}%{_sysconfdir}/profile.d/
install -m 0644 settings/ico_weston.sh  %{buildroot}%{_sysconfdir}/profile.d/
install -m 0644 settings/ico_weston.csh  %{buildroot}%{_sysconfdir}/profile.d/

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%license COPYING
%dir %{_libdir}/weston/
%{_libdir}/weston/*.so
%{_libdir}/libico-uxf-weston-plugin.so.*
%{weston_conf}/weston.ini
%{_sysconfdir}/profile.d/ico_weston.sh
%{_sysconfdir}/profile.d/ico_weston.csh

%files devel
%defattr(-,root,root,-)
%{_includedir}/%{name}/desktop-shell-client-protocol.h
%{_includedir}/%{name}/input-method-client-protocol.h
%{_includedir}/%{name}/workspaces-client-protocol.h
%{_includedir}/%{name}/ico_input_mgr-client-protocol.h
%{_includedir}/%{name}/ico_window_mgr-client-protocol.h
%{_includedir}/%{name}/ico_input_mgr.h
%{_libdir}/libico-uxf-weston-plugin.so

%changelog
* Wed Aug 21 2013 Shibata Makoto <shibata@mac.tec.toyota.co.jp> accepted/2.0alpha-wayland/20130612.174820@6b54175
- 0.7.02 release
- Fix spec file.
-- Correct package BuildRequires fields to address build errors.
-- Corrected package group tags based on Tizen packaging guidelines.
-- Add missing tag and scriptlets.
-- Remove items that are not necessary.
* Mon Jul 29 2013 Shibata Makoto <shibata@mac.tec.toyota.co.jp> accepted/2.0alpha-wayland/20130612.174820@2b428f3
- 0.7.01 release
- Following corrections for TizenIVI 3.0M1.
-- Correspondence to version up of Wayland/Weston(1.1.x => 1.2.0).
-- A change and the addition of the Wayland expansion protocol for referencePF version 0.9.
-- Correspondence to start method of TizenIVI 3.0(system V => systemd).
- 0.5.07 release
- fix for - When application generated Surface, ico_ivi_shell may not notify application of configure event.
* Wed Jun 12 2013 Shibata Makoto <shibata@mac.tec.toyota.co.jp> submit/2.0alpha-wayland/20130612.051916@5247a37
- 0.5.06 release
- fixed for - weston loop forever when a fading animation is required in an animation of the fading again by same surface.
- changed the animation of a default defined from "none" to "fade" in configuration file(weston_ivi_plugin.ini), and changed animation time in 500ms from 600ms.
* Wed Jun 12 2013 Shibata Makoto <shibata@mac.tec.toyota.co.jp> accepted/2.0alpha-wayland/20130606.173106@48a3aa7
- 0.5.05 release
- fixed for - During operation, a menu thumbnail may be non-displayed.
- Work around - Change the interface of the window animation.
* Tue Jun  4 2013 Shibata Makoto <shibata@mac.tec.toyota.co.jp> accepted/2.0alpha-wayland/20130603.172729@9aedceb
- 0.5.04 release.
- Work around - Addition of the window animation interface and window animation plugin(ico_window_animation).
* Fri May 24 2013 Shibata Makoto <shibata@mac.tec.toyota.co.jp> accepted/2.0alpha-wayland/20130520.093312@6f0a9e8
- 0.5.03 release
- Fix for TIVI-976 - Sometimes weston failed to be startup with 10%% possibility.
  When there is operation of surface(resize, change of the position, etc) just after surface creation from HomeScreen,
  Weston falls in segmentation fault.
- Fix for TIVI-974 - [WLD]Failed to execute Ico-uxf-weston-plugin test script with error "No testlog directory".
  Create testlog directory in an examination script
- Work around - Addition of the window animation interface.
* Wed May 22 2013 Shibata Makoto <shibata@mac.tec.toyota.co.jp> accepted/2.0alpha-wayland/20130520.093312@8ed2bc5
- 0.5.02 release
- Fix for TIVI-803 - Enable new UI in wayland build
- Fix for TIVI-826 - weston-plugin should NOT create /root/
- Fix for TIVI-827 - Create symbolic link for /etc/rc.d/init.d/ico_weston to /etc/rc.d/rc3.d/.
- Fix for TIVI-829 - [WLD] Text input box fails to switch to Edit mode(via touch screen)
* Mon May 13 2013 Shibata Makoto <shibata@mac.tec.toyota.co.jp> submit/2.0alpha-wayland/20130426.191944@273da16
- Bug Fix.
- I547bb4cd:Removed generated Makefile.
- I7206280c:Port ico-uxf-weston-plugin to use Weston 1.0.6 plugin API.
- I9154214b:Corrected packaging of lib*.so file. It belongs in the -devel package.
- I281e9214:Do not hardcode package header install path. Use pkginclude_HEADERS instead.
- I982a05ec:Removed checks for unused programs(C++ conpiler and sed).
- I3292b6c1:Removed log files, and add .gitignore file to prevent re-commit.
- I770d797c:Removed generated source dependency files.
- I33ca73c9:No longer need to set dist-check configure flags.
- I27ba7db2:Added Weston config-parser.c again until Weston SDK exports config functions.
* Fri Apr 26 2013 Shibata Makoto <shibata@mac.tec.toyota.co.jp> 3224fe9
- Import initial.
