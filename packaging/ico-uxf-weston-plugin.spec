Name:       ico-uxf-weston-plugin
Summary:    Weston Plugins for IVI
Version:    0.5.02
Release:    1.1
Group:      System/GUI/Libraries
License:    MIT
URL:        ""
Source0:    %{name}-%{version}.tar.bz2

BuildRequires: pkgconfig(weston) >= 1.0.6
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(evas)
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(elementary)
BuildRequires: pkgconfig(ecore-wayland)
BuildRequires: aul-devel
BuildRequires: ecore-devel
Requires: weston >= 1.0.6

%description
Weston Plugins for IVI

%package devel
Summary:    Development files for %{name}
Group:      Development/GUI/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
Development files that expose the wayland extended protocols for IVI.

%prep
%setup -q -n %{name}-%{version}

%build
autoreconf --install

%autogen --prefix=/usr

%configure
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%make_install

# configurations
%define weston_conf /root/.config
mkdir -p %{buildroot}%{weston_conf}
install -m 0644 weston.ini.ico %{buildroot}%{weston_conf}
install -m 0644 weston_ivi_plugin.ini %{buildroot}%{weston_conf}

%files
%defattr(-,root,root,-)
%dir %{_libdir}/weston/
%{_libdir}/weston/*.so
%{_libdir}/libico-uxf-weston-plugin.so.*
%{weston_conf}/weston.ini.ico
%{weston_conf}/weston_ivi_plugin.ini

%files devel
%defattr(-,root,root,-)
%{_includedir}/%{name}/ico_input_mgr-client-protocol.h
%{_includedir}/%{name}/ico_ivi_shell-client-protocol.h
%{_includedir}/%{name}/ico_window_mgr-client-protocol.h
%{_libdir}/libico-uxf-weston-plugin.so
