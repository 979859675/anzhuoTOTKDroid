<?php
// Copy this file to server/config.php on the server (NOT in Git) and fill it in.
// server/config.php is ignored by Git (see .gitignore).
return [
    'db' => [
        'host' => '127.0.0.1',
        'name' => 'totk_accounts',   // the database made in the BT panel
        'user' => 'totk_accounts',
        'pass' => 'CHANGE_ME',       // the database password from the BT panel
    ],
    // (the web admin's account and password: set on the first visit to admin.php, see DEPLOY.md)
    'max_devices' => 2,   // devices per account
    'token_days' => 30,   // a login lasts this long (the app checks online at every launch)
    // the game library's key (64 hex characters): the same value as the GitHub secret ANDROID_LIB_KEY. Without it the
    // app cannot start the encrypted game library (api.php?action=libkey answers 503).
    'lib_key' => 'CHANGE_ME_64_HEX',
];
