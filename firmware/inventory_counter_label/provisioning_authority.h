#pragma once
// build.ps1 supplies the deployment's public trust anchor in its staged sketch.
// A source-only build remains safe: encrypted phone setup is disabled.
constexpr char kProvisioningAuthorityPublicKey[] = "";
