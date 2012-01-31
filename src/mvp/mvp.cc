#include <iostream>

#include <vw/Image/ImageMath.h>
#include <vw/Image/UtilityViews.h>
#include <vw/Plate/PlateFile.h>
#include <vw/Image/MaskViews.h>
#include <vw/Octave/Main.h>

#include <mvp/Config.h>
#include <mvp/MVPWorkspace.h>
#include <mvp/MVPJob.h>

#include <boost/program_options.hpp>

#if MVP_ENABLE_GEARMAN_SUPPORT
#include <mvp/GearmanWrappers.h>
#endif


using namespace vw;
using namespace vw::cartography;
using namespace vw::platefile;
using namespace vw::octave;
using namespace std;
using namespace mvp;

namespace po = boost::program_options;

struct Options {
  po::variables_map vm;
  int render_level;
  string gearman_server;
  bool dry_run;
  BBox2i tile_bbox;
};

void handle_arguments(int argc, char* argv[], Options *opts) {
  po::options_description cmd_opts("Command line options");
  cmd_opts.add_options()
    ("help,h", "Print this message")
    ("config-file,f", po::value<string>()->default_value("mvp.conf"), "Specify a pipeline configuration file")
    ("dry-run", "Print information about the workspace and then exit")
    ;

  po::options_description render_opts("Render Options");
  render_opts.add_options()
    ("col-start", po::value<int>(), "Col to start rendering at")
    ("col-end", po::value<int>(), "One past last col to render")
    ("row-start", po::value<int>(), "Row to start rendering at")
    ("row-end", po::value<int>(), "One past last row to render")
    ("render-level", po::value<int>(&opts->render_level)->default_value(-1), "Level to render at")
    ;

  po::options_description mvp_opts;
  mvp_opts.add(MVPWorkspace::program_options()).add(render_opts);

  #if MVP_ENABLE_GEARMAN_SUPPORT
  po::options_description gearman_opts("Gearman Options");
  gearman_opts.add_options()
    ("gearman-server", po::value<string>(&opts->gearman_server), "Host running gearmand")
    ;
  mvp_opts.add(gearman_opts);
  #endif

  po::options_description all_opts;
  all_opts.add(cmd_opts).add(mvp_opts);

  store(po::command_line_parser(argc, argv).options(all_opts).run(), opts->vm);

  if (opts->vm.count("help")) {
    vw_throw(vw::ArgumentErr() << all_opts);
  }

  ifstream ifs(opts->vm["config-file"].as<string>().c_str());
  if (ifs) {
    store(parse_config_file(ifs, mvp_opts), opts->vm);
  }

  notify(opts->vm);

  #if MVP_ENABLE_GEARMAN_SUPPORT
  if (opts->vm.count("gearman-server") && opts->vm["platefile-server"].as<string>() == ".") {
    vw_throw(vw::ArgumentErr() << "Error: When using gearman, you must also specify a platefile-server!");
  }
  #endif

  opts->dry_run = opts->vm.count("dry-run");

  if (opts->vm.count("col-start") || opts->vm.count("col-end") || opts->vm.count("row-start") || opts->vm.count("row-end")) {
    if (opts->vm.count("col-start") && opts->vm.count("col-end") && opts->vm.count("row-start") && opts->vm.count("row-end")) {
      opts->tile_bbox.min()[0] = opts->vm["col-start"].as<int>();
      opts->tile_bbox.max()[0] = opts->vm["col-end"].as<int>();
      opts->tile_bbox.min()[1] = opts->vm["row-start"].as<int>();
      opts->tile_bbox.max()[1] = opts->vm["row-end"].as<int>();
    } else {
      vw_throw(vw::ArgumentErr() << "Error: missing col/row start/end");
    }
  }
}

template <class BBoxT, class RealT, size_t DimN>
void print_bbox_helper(math::BBoxBase<BBoxT, RealT, DimN> const& bbox) {
  cout << "BBox = " << bbox << endl;
  cout << "width, height = " << bbox.width() << ", " << bbox.height() << endl;
}

void print_welcome(MVPWorkspace const& work, Options const& opts) {
  cout << boolalpha << endl;
  cout << "-------------------------------------" << endl;
  cout << "Welcome to the Multiple View Pipeline" << endl;
  cout << "-------------------------------------" << endl;
  cout << endl;
  cout << "Number of images loaded = " << work.num_images() << endl;
  cout << " Equal resolution level = " << work.equal_resolution_level() << endl;
  cout << "    Equal density level = " << work.equal_density_level() << endl;
  cout << endl;
  cout << "# Workspace lonlat BBox #" << endl;
  print_bbox_helper(work.lonlat_work_area());
  cout << endl;
  cout << "# Workspace tile BBox (@ level " << opts.render_level << ") #" << endl;
  print_bbox_helper(work.tile_work_area(work.equal_density_level()));
  cout << endl;
  cout << "-------------------------------------" << endl;
  cout << "        Rendering Information" << endl;
  cout << "-------------------------------------" << endl;
  cout << endl;
  cout << "Render level = " << opts.render_level << endl;
  cout << "  Use octave = " << work.user_settings().use_octave() << endl;
  cout << endl;
  cout << "# Render tile BBox #" << endl;
  print_bbox_helper(opts.tile_bbox);
  cout << endl;
  cout << "-------------------------------------" << endl;
  cout << "              Status" << endl;
  cout << "-------------------------------------" << endl;
  cout << endl;
}

void plate_tunnel(MVPWorkspace const& work, Options const& opts) {
  boost::scoped_ptr<PlateFile> pf(new PlateFile(work.result_platefile(),
                                                work.plate_georef().map_proj(),
                                                "MVP Result Plate",
                                                work.plate_georef().tile_size(),
                                                "tif", VW_PIXEL_GRAYA, VW_CHANNEL_FLOAT32));

  pf->transaction_begin("Tunnel", 3);
  pf->write_request();

  // This way that tile is easy to find...
  for (int level = 2; level < opts.render_level; level++) {
    int divisor = opts.render_level - level;
    for (int col = opts.tile_bbox.min().x() >> divisor; col <= opts.tile_bbox.max().x() >> divisor; col++) {
      for (int row = opts.tile_bbox.min().y() >> divisor; row <= opts.tile_bbox.max().y() >> divisor; row++) {
        ImageView<PixelGrayA<float32> > rendered_tile(constant_view(PixelGrayA<float32>(), 
                                                                    work.plate_georef().tile_size(), work.plate_georef().tile_size()));
        pf->write_update(rendered_tile, col, row, level);
      }
    }
  }

  pf->sync();
  pf->write_complete();
  pf->transaction_end(true);
}

void do_task_local(MVPJobRequest const& job_request, int curr_tile, int num_tiles) {
  ostringstream status;
  status << "Tile: " << curr_tile << "/" << num_tiles << " Location: [" << job_request.col() << ", " << job_request.row() << "] @" << job_request.level() << " ";
  mvpjob_process_and_write_tile(job_request, TerminalProgressCallback("mvp", status.str()));
}

int main(int argc, char* argv[])
{
  #if MVP_ENABLE_OCTAVE_SUPPORT
  start_octave_interpreter();
  #endif

  Options opts;

  try {
    handle_arguments(argc, argv, &opts); 
  } catch (const vw::ArgumentErr& e) {
    vw_out() << e.what() << endl;
    return 1;
  } 

  MVPWorkspace work(MVPWorkspace::construct_from_program_options(opts.vm));

  if (opts.render_level < 1) {
    opts.render_level = work.equal_density_level();
  }

  if (opts.tile_bbox.empty()) {
    opts.tile_bbox = work.tile_work_area(opts.render_level);
  }

  print_welcome(work, opts);

  if (opts.dry_run) {
    vw_out() << "Dry run requested. Exiting..." << endl;
    return 0;
  }

  #if MVP_ENABLE_GEARMAN_SUPPORT
  GearmanClientWrapper gclient;
 
  if (!opts.gearman_server.empty()) {
    try {
      gclient.add_servers(opts.gearman_server);
      //TODO: Set client timeout?
    } catch (vw::GearmanErr const& e) {
      vw_out() << e.what() << endl;
      return 1;
    }
  }

  GearmanTaskList tasks(gclient);
  #endif

  plate_tunnel(work, opts);

  int curr_tile = 0;
  int num_tiles = opts.tile_bbox.width() * opts.tile_bbox.height();

  for (int col = opts.tile_bbox.min().x(); col < opts.tile_bbox.max().x(); col++) {
    for (int row = opts.tile_bbox.min().y(); row < opts.tile_bbox.max().y(); row++) {
      #if MVP_ENABLE_GEARMAN_SUPPORT
      if (gclient.is_connected()) {
        tasks.add_task(work.assemble_job(col, row, opts.render_level), curr_tile, num_tiles);
        while (tasks.has_queued_tasks()) {
          tasks.print_statuses();
          sleep(1);
        }
      } else {
        do_task_local(work.assemble_job(col, row, opts.render_level), curr_tile, num_tiles);
      }
      #else
      do_task_local(work.assemble_job(col, row, opts.render_level), curr_tile, num_tiles);
      #endif
      curr_tile++;
    }
  }

  #if MVP_ENABLE_GEARMAN_SUPPORT
  while (tasks.has_running_tasks()) {
    tasks.print_statuses();
    sleep(1);
  }
  #endif

  vw_out() << endl << "Done." << endl << endl;

  #if MVP_ENABLE_OCTAVE_SUPPORT
  do_octave_atexit();
  #endif

  return 0;
}
