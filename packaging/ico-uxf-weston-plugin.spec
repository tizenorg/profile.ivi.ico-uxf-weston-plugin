Name:       ico-uxf-weston-plugin
Summary:    Weston Plugins for IVI
Version:    0.9.21
Release:    1.1
Group:      Graphics & UI Framework/Automotive UI
License:    MIT
URL:        ""
Source0:    %{name}-%{version}.tar.bz2

BuildRequires: pkgconfig(weston) >= 1.4.0
BuildRequires: pkgconfig(pixman-1)
BuildRequires: pkgconfig(xkbcommon) >= 0.0.578
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(evas)
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(elementary)
BuildRequires: pkgconfig(ecore-wayland)
BuildRequires: pkgconfig(aul)
BuildRequires: pkgconfig(ecore)
BuildRequires: mesa-devel
BuildRequires: genivi-shell-devel
BuildRequires: weston-ivi-shell-devel
Requires: weston >= 1.4.0
Requires: genivi-shell
Requires: weekeyboard
Conflicts: weston-ivi-config

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

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%dir %{_libdir}/weston/
%{_libdir}/weston/*.so
%{_libdir}/libico-uxf-weston-plugin.so.*
%{weston_conf}/weston.ini

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/%{name}/ico_input_mgr-client-protocol.h
%{_includedir}/%{name}/ico_window_mgr-client-protocol.h
%{_includedir}/%{name}/ico_input_mgr.h
%{_bindir}/ico_send_inputevent
%{_libdir}/libico-uxf-weston-plugin.so
