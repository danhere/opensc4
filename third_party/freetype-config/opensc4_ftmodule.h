/* FreeType's module list for core.dll (third_party/CMakeLists.txt): the
   TrueType and CFF drivers, what they need, auto-hinting and the smooth
   (anti-aliased) renderer; Type 1 for the game's .mxf faces (PFB files
   inside, unscrambled), TrueType and CFF for Windows's faces and the
   OpenType faces a player may add. */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t1_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
