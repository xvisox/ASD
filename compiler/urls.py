from django.urls import path

from . import views

app_name = 'compiler'
urlpatterns = [
    path('', views.IndexView.as_view(), name='index'),
    path('edit', views.EditView.as_view(), name='edit'),
    path('file', views.FileView.as_view(), name='file'),
    path('files/<int:pk>', views.ShowFileView.as_view(), name='show_file'),
    path('files/<int:file_info_id>/remove', views.remove_file, name='remove_file'),
    path('files/add/directory', views.add_directory, name='add_directory')
]
