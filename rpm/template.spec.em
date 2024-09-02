%bcond_without tests
%bcond_without weak_deps

%global __os_install_post %(echo '%{__os_install_post}' | sed -e 's!/usr/lib[^[:space:]]*/brp-python-bytecompile[[:space:]].*$!!g')
%global __provides_exclude_from ^@(InstallationPrefix)/.*$
%global __requires_exclude_from ^@(InstallationPrefix)/.*$

Name:           @(Package)
Version:        @(Version)
Release:        @(RPMInc)%{?dist}%{?release_suffix}
Summary:        ROS @(Name) package

License:        @(License)
@[if Homepage and Homepage != '']URL:            @(Homepage)@\n@[end if]@
Source0:        %{name}-%{version}.tar.gz
@[if NoArch]@\nBuildArch:      noarch@\n@[end if]@

@[for p in Depends]Requires:       @p@\n@[end for]@
@[for p in BuildDepends]BuildRequires:  @p@\n@[end for]@
@[for p in Conflicts]Conflicts:      @p@\n@[end for]@
@[for p in Replaces]Obsoletes:      @p@\n@[end for]@
@[for p in Provides]Provides:       @p@\n@[end for]@
@[if TestDepends]@\n%if 0%{?with_tests}
@[for p in TestDepends]BuildRequires:  @p@\n@[end for]@
%endif@\n@[end if]@
@[if Supplements]@\n%if 0%{?with_weak_deps}
@[for p in Supplements]Supplements:    @p@\n@[end for]@
%endif@\n@[end if]@

%description
@(Description)

%prep
%autosetup -p1

%build
# override macro
%define __meson_auto_features auto
# In case we're installing to a non-standard location, look for a setup.sh
# in the install tree and source it.  It will set things like
# CMAKE_PREFIX_PATH, PKG_CONFIG_PATH, and PYTHONPATH.
if [ -f "@(InstallationPrefix)/setup.sh" ]; then . "@(InstallationPrefix)/setup.sh"; fi
# call meson executable instead of using the 'meson' macro to use default paths
%__meson setup \
    --prefix="@(InstallationPrefix)" \
    --cmake-prefix-path="@(InstallationPrefix)" \
    --libdir=lib \
    --libexecdir=lib \
    %{_vpath_builddir}
%meson_build

%define __spec_install_post \
    %{?__debug_package:%{__debug_install_post}} \
    %{__arch_install_post} \
    %{__os_install_post} \
    %{_builddir}/%{name}-%{version}/src/ipa/ipa-sign-install.sh %{_builddir}/%{name}-%{version}/%{_vpath_builddir}/src/ipa-priv-key.pem %{buildroot}/@(InstallationPrefix)/lib/libcamera/ipa_*.so \
%{nil}

%install
# In case we're installing to a non-standard location, look for a setup.sh
# in the install tree and source it.  It will set things like
# CMAKE_PREFIX_PATH, PKG_CONFIG_PATH, and PYTHONPATH.
if [ -f "@(InstallationPrefix)/setup.sh" ]; then . "@(InstallationPrefix)/setup.sh"; fi
%meson_install

%if 0%{?with_tests}
%check
# Look for a Makefile target with a name indicating that it runs tests
TEST_TARGET=$(%__ninja -C %{_vpath_builddir} -t targets | sed "s/^\(test\|check\):.*/\\1/;t f;d;:f;q0")
if [ -n "$TEST_TARGET" ]; then
# In case we're installing to a non-standard location, look for a setup.sh
# in the install tree and source it.  It will set things like
# CMAKE_PREFIX_PATH, PKG_CONFIG_PATH, and PYTHONPATH.
if [ -f "@(InstallationPrefix)/setup.sh" ]; then . "@(InstallationPrefix)/setup.sh"; fi
%meson_test || echo "RPM TESTS FAILED"
else echo "RPM TESTS SKIPPED"; fi
%endif

%files
@[for lf in LicenseFiles]%license @lf@\n@[end for]@
@(InstallationPrefix)

%changelog@
@[for change_version, (change_date, main_name, main_email) in changelogs]
* @(change_date) @(main_name) <@(main_email)> - @(change_version)
- Autogenerated by Bloom
@[end for]
