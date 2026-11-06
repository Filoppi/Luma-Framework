# Streamline

Headers from NVIDIA Streamline 2.14.1 (https://github.com/NVIDIA-RTX/Streamline), used by DLSS Frame Generation ("UseLumaDLSSG", see "Source/Core/dlss/DLSSG.cpp").

The runtime dlls are loaded from next to the Luma addon. Copy them into "bin/x64" from the production ("bin/x64") folder of the Streamline 2.14.1 release zip, the build and packaging copy them from there:
- sl.interposer.dll
- sl.common.dll
- sl.dlss_g.dll
- sl.reflex.dll
- sl.pcl.dll
- nvngx_dlssg.dll

Their signature is verified before loading them, so they need to be the NVIDIA signed ones.
