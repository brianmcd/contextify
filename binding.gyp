{
  'targets': [
    {
      'target_name': 'contextify',
      'include_dirs': ["<!(node -p -e \"require('path').relative('.', require('path').dirname(require.resolve('nan')))\")"],
      'sources': [ 'src/contextify.cc' ]
    }
  ]
}
