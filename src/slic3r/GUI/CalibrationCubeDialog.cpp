#include "CalibrationCubeDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "GLCanvas3D.hpp"
#include "GUI.hpp"
#include "GUI_ObjectList.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include <wx/scrolwin.h>
#include <wx/display.h>
#include <wx/file.h>
#include "wxExtensions.hpp"

#if ENABLE_SCROLLABLE
static wxSize get_screen_size(wxWindow* window)
{
    const auto idx = wxDisplay::GetFromWindow(window);
    wxDisplay display(idx != wxNOT_FOUND ? idx : 0u);
    return display.GetClientArea().GetSize();
}
#endif // ENABLE_SCROLLABLE

namespace Slic3r {
namespace GUI {

void CalibrationCubeDialog::create_buttons(wxStdDialogButtonSizer* buttons){
    wxString choices_scale[] = { "10", "20", "30", "40" };
    scale = new wxComboBox(this, wxID_ANY, wxString{ "20" }, wxDefaultPosition, wxDefaultSize, 4, choices_scale);
    scale->SetToolTip(_L("You can choose the dimension of the cube."
        " It's a simple scale, you can modify it in the right panel yourself if you prefer. It's just quicker to select it here."));
    scale->SetSelection(1);
    wxString choices_goal[] = { "Dimensional accuracy (default)" , "infill/perimeter overlap"/*, "external perimeter overlap"*/};
    calibrate = new wxComboBox(this, wxID_ANY, _L("Dimensional accuracy (default)"), wxDefaultPosition, wxDefaultSize, 2, choices_goal);
    calibrate->SetToolTip(_L("Select a goal, this will change settings to increase the effects to search."));
    calibrate->SetSelection(0);
    calibrate->SetEditable(false);

    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Dimension:")));
    buttons->AddSpacer(10);
    buttons->Add(scale);
    buttons->AddSpacer(10);
    buttons->Add(new wxStaticText(this, wxID_ANY, _L("mm")));
    buttons->AddSpacer(40);
    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Goal:")));
    buttons->Add(calibrate);
    buttons->AddSpacer(40);

    wxButton* bt = new wxButton(this, wxID_FILE1, _(L("Standard Cube")));
    bt->Bind(wxEVT_BUTTON, &CalibrationCubeDialog::create_geometry_standard, this);
    bt->SetToolTip(_L("Standard cubic xyz cube, with a flat top. Better for infill/perimeter overlap calibration."));
    buttons->Add(bt);
    buttons->AddSpacer(10);
    bt = new wxButton(this, wxID_FILE1, _(L("CaribouCube")));
    bt->Bind(wxEVT_BUTTON, &CalibrationCubeDialog::create_geometry_caribou, this);
    bt->SetToolTip(_L("Caribou cubic xyz cube, with a flat top. Better for infill/perimeter overlap calibration."));
    buttons->Add(bt);
    buttons->AddSpacer(10);
    bt = new wxButton(this, wxID_FILE1, _(L("Voron Cube")));
    bt->Bind(wxEVT_BUTTON, &CalibrationCubeDialog::create_geometry_voron, this);
    bt->SetToolTip(_L("Voron cubic cube with many features inside, with a bearing slot on top. Better to check dimensional accuracy."));
    buttons->Add(bt);
}

void CalibrationCubeDialog::create_geometry(std::string calibration_path) {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    plat->new_project();

    //GLCanvas3D::set_warning_freeze(true);
    std::vector<size_t> objs_idx = plat->load_files(std::vector<std::string>{
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration"/"cube"/ calibration_path).string()}, true, false, false);

    assert(objs_idx.size() == 1);
    const DynamicPrintConfig* printConfig = this->gui_app->get_tab(Preset::TYPE_PRINT)->get_config();

    float scalefactor = 0.5;
    if (calibration_path == "voron_design_cube_v7.amf")
    {
        scalefactor = 1.0 / 3.0;
    }
    int idx_scale = scale->GetSelection();
    double xyzScale = (idx_scale +1) * scalefactor;

    //do scaling
    model.objects[objs_idx[0]]->scale(xyzScale, xyzScale, xyzScale);

    //workaround to place parts on the bed: offset parts
    model.objects[objs_idx[0]]->translate({0,0,20});

    /// --- custom config ---
    int idx_goal = calibrate->GetSelection();
    if (idx_goal == 1) {
        model.objects[objs_idx[0]]->config.set_key_value("perimeters", new ConfigOptionInt(1));
        model.objects[objs_idx[0]]->config.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipCubic));
    } else if (idx_goal == 2) {
        model.objects[objs_idx[0]]->config.set_key_value("perimeters", new ConfigOptionInt(3));
        //add full solid layers
    }

     /// --- main config, please modify object config when possible ---
    DynamicPrintConfig new_print_config = *printConfig; //make a copy
    new_print_config.set_key_value("skirts", new ConfigOptionInt(2));
    new_print_config.set_key_value("skirt_distance", new ConfigOptionFloat(1.0));

    //update plater
    this->gui_app->get_tab(Preset::TYPE_PRINT)->load_config(new_print_config);
    plat->on_config_change(new_print_config);
    plat->changed_objects(objs_idx);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->update_dirty();
    plat->is_preview_shown();

    //update everything, easier to code.
    ObjectList* obj = this->gui_app->obj_list();
    obj->update_after_undo_redo();

    plat->reslice();
    plat->select_view_3D("Preview");
    }

} // namespace GUI
} // namespace Slic3r
